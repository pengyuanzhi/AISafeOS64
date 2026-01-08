/**
 * @file motor-control.c
 * @brief Motor Control Application Example
 *
 * Simple real-time motor control application demonstrating:
 * - Main loop with periodic execution
 * - Sensor reading
 * - PID control algorithm
 * - Actuator output
 * - System calls for I/O
 *
 * @note AISafe64 User Application
 * @version 1.0
 * @date 2025-01-08
 */

#include <stdint.h>
#include <stddef.h>

/*
 * System Call Definitions
 *
 * Applications interact with the kernel through system calls.
 * These are invoked via the SVC (Supervisor Call) instruction.
 */

/* System call numbers */
#define SYSCALL_YIELD 0   /* Yield CPU to scheduler */
#define SYSCALL_SLEEP 1   /* Sleep for specified milliseconds */
#define SYSCALL_WRITE 2   /* Write to console/device */
#define SYSCALL_READ 3    /* Read from device */
#define SYSCALL_EXIT 4    /* Exit application */
#define SYSCALL_MQ_OPEN 5 /* Open message queue */
#define SYSCALL_MQ_SEND 6 /* Send message */
#define SYSCALL_MQ_RECV 7 /* Receive message */

/*
 * System Call Wrappers
 *
 * These functions provide convenient interfaces to system calls.
 * They use inline assembly to invoke the SVC instruction.
 */

/**
 * @brief Yield CPU to scheduler
 */
static inline void syscall_yield(void)
{
    asm volatile("svc #0" ::: "memory");
}

/**
 * @brief Sleep for specified milliseconds
 * @param ms Sleep duration in milliseconds
 */
static inline void syscall_sleep(uint32_t ms)
{
    register uint64_t x0 __asm("x0") = SYSCALL_SLEEP;
    register uint64_t x1 __asm("x1") = ms;
    asm volatile("svc #0" : : "r"(x0), "r"(x1) : "memory");
}

/**
 * @brief Write data to console
 * @param buf Data buffer
 * @param len Length of data
 * @return Number of bytes written, or negative error code
 */
static inline ssize_t syscall_write(const char *buf, size_t len)
{
    register uint64_t x0 __asm("x0") = SYSCALL_WRITE;
    register uint64_t x1 __asm("x1") = (uint64_t)buf;
    register uint64_t x2 __asm("x2") = len;
    register ssize_t ret __asm("x0");

    asm volatile("svc #0" : "=r"(ret) : "r"(x0), "r"(x1), "r"(x2) : "memory");

    return ret;
}

/**
 * @brief Exit application
 * @param code Exit code
 */
static inline void syscall_exit(int code) __attribute__((noreturn));

static inline void syscall_exit(int code)
{
    register uint64_t x0 __asm("x0") = SYSCALL_EXIT;
    register uint64_t x1 __asm("x1") = (uint64_t)code;
    asm volatile("svc #0" : : "r"(x0), "r"(x1) : "memory");
    __builtin_unreachable();
}

/*
 * Hardware Abstraction Layer
 *
 * These functions provide access to hardware devices.
 * In a real system, these would be implemented as device driver calls.
 */

/**
 * @brief Read motor sensor (e.g., encoder)
 * @return Current position in encoder counts
 */
static uint32_t read_motor_sensor(void)
{
    /* TODO: Implement actual sensor reading */
    /* For now, simulate sensor data */
    static uint32_t pos = 0U;
    pos = (pos + 1U) % 10000U;
    return pos;
}

/**
 * @brief Set motor output (PWM)
 * @param output PWM duty cycle (0-100%)
 */
static void set_motor_output(uint32_t output)
{
    /* TODO: Implement actual PWM output */
    (void)output;
}

/**
 * @brief PID controller for motor control
 * @param setpoint Desired position
 * @param measured Current position
 * @return Control output (PWM duty cycle)
 */
static int32_t motor_pid_control(uint32_t setpoint, uint32_t measured)
{
    /* PID constants */
    static const int32_t Kp = 10; /* Proportional gain */
    static const int32_t Ki = 1;  /* Integral gain */
    static const int32_t Kd = 5;  /* Derivative gain */

    /* State variables */
    static int32_t integral = 0;
    static int32_t prev_error = 0;

    /* Calculate error */
    int32_t error = (int32_t)setpoint - (int32_t)measured;

    /* Update integral */
    integral += error;

    /* Calculate derivative */
    int32_t derivative = error - prev_error;
    prev_error = error;

    /* Calculate output */
    int32_t output = (Kp * error) + (Ki * integral) + (Kd * derivative);

    /* Clamp output */
    if (output > 100) {
        output = 100;
    } else if (output < -100) {
        output = -100;
    }

    return output;
}

/*
 * String Functions
 *
 * Simple implementations of standard string functions.
 * Applications cannot use libc (freestanding environment).
 */

/**
 * @brief Calculate string length
 * @param s String
 * @return Length of string
 */
static size_t strlen(const char *s)
{
    size_t len = 0U;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

/**
 * @brief Convert integer to string
 * @param value Integer value
 * @param str Output buffer
 * @param size Buffer size
 * @return Number of characters written
 */
static int int_to_str(int32_t value, char *str, size_t size)
{
    char tmp[16];
    int i = 0;
    int neg = 0;
    int count = 0;

    if (value < 0) {
        neg = 1;
        value = -value;
    }

    /* Convert to string (reverse) */
    do {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);

    /* Add sign if negative */
    if (neg) {
        tmp[i++] = '-';
    }

    /* Copy to output (reverse back) */
    if ((size_t)i < size) {
        int j;
        for (j = i - 1; j >= 0; j--) {
            str[count++] = tmp[j];
        }
        str[count] = '\0';
    }

    return count;
}

/*
 * Main Application Entry Point
 *
 * This is the entry point called by the application loader.
 * It should never return (exit via syscall_exit instead).
 */

/**
 * @brief Motor control application main function
 * @param argc Argument count
 * @param argv Argument values
 * @return Exit code (should never return)
 */
int app_main(int argc, char *argv[])
{
    const char *msg = "Motor Control Application v1.0\n";
    uint32_t setpoint = 5000U; /* Desired position */
    char buffer[128];
    int len;
    uint32_t iterations = 0U;

    (void)argc;
    (void)argv;

    /* Print startup message */
    syscall_write(msg, strlen(msg));

    /* Main control loop */
    while (1) {
        uint32_t sensor_value;
        int32_t control_output;

        /* Read sensor */
        sensor_value = read_motor_sensor();

        /* Calculate control output */
        control_output = motor_pid_control(setpoint, sensor_value);

        /* Set motor output */
        if (control_output >= 0) {
            set_motor_output((uint32_t)control_output);
        } else {
            /* Negative output means reverse direction */
            /* TODO: Implement direction control */
            set_motor_output(0U);
        }

        /* Log status every 100 iterations */
        if ((iterations % 100U) == 0U) {
            len = int_to_str((int32_t)sensor_value, buffer, sizeof(buffer));
            syscall_write("Position: ", 10);
            syscall_write(buffer, (size_t)len);
            syscall_write("\n", 1);
        }

        iterations++;

        /* Sleep for 10ms (100Hz control loop) */
        syscall_sleep(10);
    }

    /* Should never reach here */
    syscall_exit(0);
    __builtin_unreachable();
}

/*
 * Initialization and Cleanup
 *
 * These functions are called by the application loader
 * before and after app_main.
 */

void app_init(void)
{
    /* Application initialization */
    /* Called before app_main */
}

void app_cleanup(void)
{
    /* Application cleanup */
    /* Called after app_main exits */
}
