#!/bin/bash
#
# sign_ecdsa.sh - ECDSA-P256 Signature Tool
#
# Signs ELF files using ECDSA-P256 private key.
# Generates SHA-256 hash and creates signature.
#
# Usage: sign_ecdsa.sh -k <key_file> -i <input_file> -o <output_file>
#
# @note AISafe64 Application Loader
# @version 1.0
# @date 2025-01-08

set -e

# Defaults
PRIVATE_KEY=""
INPUT_FILE=""
OUTPUT_FILE=""
VERBOSE=0

# Print usage
usage() {
    echo "Usage: $0 -k <private_key> -i <input_file> -o <output_file>"
    echo ""
    echo "Options:"
    echo "  -k <file>   Private key file (PEM format)"
    echo "  -i <file>   Input ELF file to sign"
    echo "  -o <file>   Output signature file"
    echo "  -v          Verbose mode"
    echo "  -h          Show this help"
    echo ""
    echo "Example:"
    echo "  $0 -k private_key.pem -i app.elf -o app.sig"
}

# Parse arguments
while getopts "k:i:o:vh" opt; do
    case $opt in
        k)
            PRIVATE_KEY="$OPTARG"
            ;;
        i)
            INPUT_FILE="$OPTARG"
            ;;
        o)
            OUTPUT_FILE="$OPTARG"
            ;;
        v)
            VERBOSE=1
            ;;
        h)
            usage
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done

# Check required arguments
if [ -z "$PRIVATE_KEY" ] || [ -z "$INPUT_FILE" ] || [ -z "$OUTPUT_FILE" ]; then
    echo "Error: Missing required arguments"
    usage
    exit 1
fi

# Check if private key exists
if [ ! -f "$PRIVATE_KEY" ]; then
    echo "Error: Private key file not found: $PRIVATE_KEY"
    exit 1
fi

# Check if input file exists
if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: Input file not found: $INPUT_FILE"
    exit 1
fi

# Verbose output
if [ $VERBOSE -eq 1 ]; then
    echo "Signing ELF file..."
    echo "  Private key: $PRIVATE_KEY"
    echo "  Input file:  $INPUT_FILE"
    echo "  Output file: $OUTPUT_FILE"
fi

# Calculate SHA-256 hash
if [ $VERBOSE -eq 1 ]; then
    echo "Calculating SHA-256 hash..."
fi

HASH=$(sha256sum "$INPUT_FILE" | cut -d' ' -f1)

if [ $VERBOSE -eq 1 ]; then
    echo "  Hash: $HASH"
fi

# Create hash file for signing
HASH_FILE=$(mktemp)
echo "$HASH" > "$HASH_FILE"

# Sign hash using OpenSSL ECDSA
if [ $VERBOSE -eq 1 ]; then
    echo "Generating ECDSA-P256 signature..."
fi

SIGNATURE_DER=$(openssl dgst -sha256 -sign "$PRIVATE_KEY" "$INPUT_FILE" | base64)

# Convert DER signature to raw R,S format (64 bytes)
SIGNATURE_RAW=$(openssl dgst -sha256 -sign "$PRIVATE_KEY" "$INPUT_FILE" | \
               openssl asn1parse -inform DER -i /dev/stdin 2>/dev/null | \
               grep -E "INTEGER:[0-9]+" | \
               awk '{print $3}' | \
               tr -d ':' | \
                   xxd -r -p | \
                   base64)

# Alternative: Use Python for proper R,S extraction
SIGNATURE_HEX=$(python3 -c "
import hashlib
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.backends import default_backend

# Read private key
with open('$PRIVATE_KEY', 'rb') as f:
    private_key = serialization.load_pem_private_key(
        f.read(),
        password=None,
        backend=default_backend()
    )

# Read file and calculate hash
with open('$INPUT_FILE', 'rb') as f:
    data = f.read()

hash_obj = hashlib.sha256(data)
hash_bytes = hash_obj.digest()

# Sign
signature = private_key.sign(
    hash_bytes,
    ec.ECDSA(hashes.SHA256())
)

# Extract R and S (assuming DER format)
# DER format: 0x30 [total_len] 0x02 [R_len] [R] 0x02 [S_len] [S]
if signature[0] == 0x30:
    r_len = signature[3]
    s_start = 4 + r_len
    s_len = signature[s_start + 1]

    R = signature[4:4+r_len]
    S = signature[s_start+2:s_start+2+s_len]

    # Pad to 32 bytes each
    R = R.rjust(32, b'\x00')
    S = S.rjust(32, b'\x00')

    # Combine and convert to hex
    signature_raw = R + S
    print(signature_raw.hex())
else:
    print('Error: Unexpected signature format', file=sys.stderr)
    sys.exit(1)
" 2>/dev/null)

# Fallback: Simple OpenSSL output (hex string)
if [ -z "$SIGNATURE_HEX" ]; then
    echo "Warning: Using simplified signature format" >&2
    SIGNATURE_HEX=$(openssl dgst -sha256 -sign "$PRIVATE_KEY" "$INPUT_FILE" | \
                    xxd -p -c 256 | tr -d '\n')
fi

# Write signature to output file
echo "$SIGNATURE_HEX" > "$OUTPUT_FILE"

# Clean up
rm -f "$HASH_FILE"

# Verify signature
if [ $VERBOSE -eq 1 ]; then
    echo "Verifying signature..."
fi

# Extract public key from private key
PUBLIC_KEY=$(openssl ec -in "$PRIVATE_KEY" -pubout -outform DER 2>/dev/null | \
            tail -c 65 | base64)

# Verify
if openssl dgst -sha256 -verify <(openssl ec -in "$PRIVATE_KEY" -pubout -outform PEM 2>/dev/null) \
        -signature "$OUTPUT_FILE" "$INPUT_FILE" >/dev/null 2>&1; then
    if [ $VERBOSE -eq 1 ]; then
        echo "Signature verification: SUCCESS"
    fi
else
    echo "Warning: Signature verification failed" >&2
fi

if [ $VERBOSE -eq 1 ]; then
    echo "Signature written to: $OUTPUT_FILE"
    echo "  Length: $(wc -c < "$OUTPUT_FILE") bytes"
fi

echo "Done"

exit 0
