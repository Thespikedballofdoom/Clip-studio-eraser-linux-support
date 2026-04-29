//the 24th. now with args.
//it matches partial name.
//Usage: ./kt24 "Optical Mecha forward"
//via https://chat.deepseek.com/a/chat/s/c9cdf7b2-f28d-4f79-a0af-43a595960449
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>

///input-event-codes.h
#define WACOM_TIP_DOWN 330
#define WACOM_BUTTON 331
#define KEYBOARD_CTRL 29
#define KEY_RIGHTCTRL 97
#define KEYBOARD_Z 44
#define KEYBOARD_SHIFT 42
#define CHECK_INTERVAL 1
#define CTRL_DELAY_MS 400

volatile sig_atomic_t keep_running = 1;
bool shift_pressed = false;
bool shift_fresh = true;
volatile bool tablet_ctrl_active = false;
volatile bool keyboard_ctrl_active = false;

// Thread control for delayed release
volatile bool delayed_release_active = false;
pthread_t delayed_release_thread;

// Global keyboard device
int keyboard_fd = -1;
const char *keyboard_name_pattern = NULL;

void print_usage(const char *program_name) {
    printf("Usage: %s <device-name-pattern>\n", program_name);
    printf("Example: %s \"CORSAIR K70\"\n", program_name);
    printf("Example: %s \"Razer Basilisk\"\n", program_name);
    printf("\nAvailable input devices:\n");

    DIR *dir;
    struct dirent *ent;
    char path[256];
    char name[256] = "???";
    int count = 0;

    if ((dir = opendir("/dev/input/")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "event", 5) == 0) {
                snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
                int fd = open(path, O_RDONLY | O_NONBLOCK);
                if (fd == -1) continue;

                ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                close(fd);

                printf("  %-60s - %s\n", name, path);
                count++;
            }
        }
        closedir(dir);
    }

    if (count == 0) {
        printf("  No input devices found!\n");
    }
}

int find_keyboard_device(const char *pattern) {
    DIR *dir;
    struct dirent *ent;
    char path[256];
    char name[256] = "???";
    int fd = -1;

    if ((dir = opendir("/dev/input/")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "event", 5) == 0) {
                snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
                int test_fd = open(path, O_RDONLY | O_NONBLOCK);
                if (test_fd == -1) continue;

                ioctl(test_fd, EVIOCGNAME(sizeof(name)), name);

                // Check if name contains the pattern
                if (strstr(name, pattern) != NULL) {
                    close(test_fd); // Close the read-only fd

                    // Re-open for read/write
                    fd = open(path, O_RDWR | O_NONBLOCK);
                    if (fd != -1) {
                        printf("Found input device: %s (%s)\n", name, path);
                    } else {
                        fprintf(stderr, "Failed to open %s for read/write: ", path);
                        perror("");
                    }
                    break;
                } else {
                    close(test_fd);
                }
            }
        }
        closedir(dir);
    }

    return fd;
}

void handle_signal(int sig) {
    keep_running = 0;
    tablet_ctrl_active = false;
    delayed_release_active = false;
}

void execute_command(const char *command) {
    printf("Executing: %s\n", command);
    if (fork() == 0) {
        execlp("/bin/sh", "sh", "-c", command, NULL);
        exit(EXIT_FAILURE);
    }
}

void send_direct_key_event(int key_code, int value) {
    if (keyboard_fd == -1) {
        printf("ERROR: Keyboard device not open, cannot send key event\n");
        return;
    }

    struct input_event ev;
    memset(&ev, 0, sizeof(ev));

    // Get current time
    gettimeofday(&ev.time, NULL);

    // Key event
    ev.type = EV_KEY;
    ev.code = key_code;
    ev.value = value; // 1 for press, 0 for release
    write(keyboard_fd, &ev, sizeof(ev));

    // Sync event
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(keyboard_fd, &ev, sizeof(ev));

    printf("Sent direct key event: code=%d, value=%d to input device\n", key_code, value);
}

bool check_ktabletconfig_xsetwacom() {
    FILE *fp, *fp2;
    char path[256];
    bool result = false;

    // First check ktabletconfig
    fp = popen("ktabletconfig --binding --stylus \"Wacom Intuos BT S Pen\" \"--button\" 3", "r");
    if (fp != NULL) {
        while (fgets(path, sizeof(path), fp) != NULL) {
            if (strstr(path, "Key,Control") != NULL) {
                result = true;
                break;
            }
        }
        pclose(fp);
    } else {
        printf("Failed to run ktablet command\n");
    }

    // If ktabletconfig didn't find it, check xsetwacom
    if (!result) {
        fp2 = popen("xsetwacom --get \"Wacom Intuos BT S Pen stylus\" \"button 2\"", "r");
        if (fp2 != NULL) {
            while (fgets(path, sizeof(path), fp2) != NULL) {
                if (strstr(path, "+Control_L") != NULL) {
                    result = true;
                    break;
                }
            }
            pclose(fp2);
        } else {
            printf("Failed to run xsetwacom command\n");
        }
    }

    return result;
}

void* ctrl_press_doubletrick(void* arg) {
    printf("Performing CTRL double-trick\n");
    send_direct_key_event(KEY_RIGHTCTRL, 1);
    send_direct_key_event(KEY_RIGHTCTRL, 0);
    send_direct_key_event(KEY_RIGHTCTRL, 1);
    return NULL;
}

void* delayed_release_function(void* arg) {
    printf("Starting delayed release thread (20ms)\n");
    delayed_release_active = true;

    usleep(20000); // 20ms delay

    // Only execute if we're still supposed to (no keyboard CTRL was pressed)
    if (delayed_release_active) {
        printf("Executing delayed CTRL release\n");
        execute_command("xdotool keyup Control_R");
        send_direct_key_event(KEYBOARD_CTRL, 0);
        send_direct_key_event(KEY_RIGHTCTRL, 0);
    } else {
        printf("Delayed release cancelled (keyboard CTRL pressed)\n");
    }

    delayed_release_active = false;
    return NULL;
}

bool is_wacom_device(const char *device_name) {
    return strstr(device_name, "Wacom") != NULL;
}

int open_device_readonly(const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("Error opening device");
    }
    return fd;
}

void find_wacom_devices(int *wacom_fds, int *num_wacom) {
    DIR *dir;
    struct dirent *ent;
    char path[256];
    char name[256] = "???";

    *num_wacom = 0;

    if ((dir = opendir("/dev/input/")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "event", 5) == 0) {
                snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
                int fd = open_device_readonly(path);
                if (fd == -1) continue;

                ioctl(fd, EVIOCGNAME(sizeof(name)), name);

                if (is_wacom_device(name) && *num_wacom < 10) {
                    wacom_fds[(*num_wacom)++] = fd;
                    printf("Found Wacom device: %s (%s)\n", name, path);
                } else {
                    close(fd);
                }
            }
        }
        closedir(dir);
    } else {
        perror("Could not open /dev/input/");
    }
}

void process_event(struct input_event *ev, bool *tip_pressed, bool *button_pressed,
                   bool *ctrl_pressed, bool *z_pressed) {
    if (ev->type == EV_KEY) {
        switch (ev->code) {
            case WACOM_TIP_DOWN:
                *tip_pressed = (ev->value == 1);
                break;
            case WACOM_BUTTON:
                // Handle tablet CTRL button (331) - ONLY on release
                if (ev->value == 1) {
                    // Just track the state, no action on press
                    tablet_ctrl_active = true;
                    printf("Tablet CTRL pressed - tracking state only\n");
                } else if (ev->value == 0) {
                    // Button released - handle based on keyboard state
                    tablet_ctrl_active = false;
                    printf("#### 331 RELEASE DETECTED - ");

                    if (keyboard_ctrl_active) {
                        // Keyboard CTRL is still held - perform double-trick immediately
                        printf("keyboard CTRL still active, performing CTRL double-trick\n");
                        pthread_t trick_thread;
                        pthread_create(&trick_thread, NULL, ctrl_press_doubletrick, NULL);
                        pthread_detach(trick_thread);
                    } else {
                        // No keyboard CTRL active - start delayed release thread
                        printf("no keyboard CTRL active, starting delayed release thread\n");
                        delayed_release_active = false; // Ensure previous thread is marked inactive
                        pthread_create(&delayed_release_thread, NULL, delayed_release_function, NULL);
                        pthread_detach(delayed_release_thread);
                    }
                }
                *button_pressed = (ev->value == 1);
                break;
            case KEYBOARD_CTRL:
                // Update keyboard CTRL state
                bool was_ctrl_active = keyboard_ctrl_active;
                keyboard_ctrl_active = (ev->value == 1 || ev->value == 2);
                *ctrl_pressed = keyboard_ctrl_active;

                if (ev->value == 1 && delayed_release_active) {
                    // Keyboard CTRL pressed while delayed release is active - cancel it
                    printf("Keyboard CTRL pressed - cancelling delayed release\n");
                    delayed_release_active = false;
                }

                if (ev->value == 0) {
                    // Keyboard CTRL released
                    if (tablet_ctrl_active) {
                        // Tablet CTRL is still held - send CTRL down via xdotool
                        printf("Keyboard CTRL released but tablet CTRL still held - sending CTRL down via xdotool\n");
                        execute_command("xdotool keydown Control_R");
                    } else {
                        // No tablet CTRL active - send release via xdotool
                        printf("Keyboard CTRL released with no tablet CTRL - sending CTRL release via xdotool\n");
                        execute_command("xdotool keyup Control_R");
                        execute_command("xdotool keyup Control_R");
                        send_direct_key_event(KEYBOARD_CTRL, 0);
                        send_direct_key_event(KEY_RIGHTCTRL, 0);
                    }
                }
                break;
            case KEYBOARD_SHIFT:
                // Update shift state
                shift_pressed = (ev->value == 1 || ev->value == 2);

                // Shift up event sets shift_fresh to true
                if (ev->value == 0) {
                    shift_fresh = true;
                    printf("Shift released - shift_fresh set to true\n");
                }

                // When SHIFT is pressed and tablet Control is held AND shift_fresh is true
                if (shift_pressed && tablet_ctrl_active && shift_fresh) {
                    printf("SHIFT pressed while tablet Control held - sending ALT once via direct events\n");
                    usleep(CTRL_DELAY_MS * 100);
                    send_direct_key_event(56, 1); // ALT key
                    usleep(CTRL_DELAY_MS * 100);
                    send_direct_key_event(56, 0); // ALT key release
                    shift_fresh = false; // Reset after sending
                    printf("ALT sent, shift_fresh set to false\n");
                }
                break;
        }
    }
                   }

                   int main(int argc, char *argv[]) {
                       if (argc != 2) {
                           print_usage(argv[0]);
                           return 1;
                       }

                       keyboard_name_pattern = argv[1];

                       // Find and open the input device by name pattern
                       keyboard_fd = find_keyboard_device(keyboard_name_pattern);
                       if (keyboard_fd == -1) {
                           fprintf(stderr, "Failed to find input device matching pattern: %s\n", keyboard_name_pattern);
                           print_usage(argv[0]);
                           return 1;
                       }

                       signal(SIGINT, handle_signal);
                       signal(SIGTERM, handle_signal);

                       int wacom_fds[10];
                       int num_wacom = 0;
                       bool tip_pressed = false;
                       bool button_pressed = false;
                       bool ctrl_pressed = false;
                       bool z_pressed = false;

                       struct pollfd fds[11]; // Up to 10 wacom devices + 1 keyboard
                       int nfds = 0;
                       struct input_event ev;
                       int ret;

                       while (keep_running) {
                           find_wacom_devices(wacom_fds, &num_wacom);

                           if (num_wacom == 0) {
                               printf("No Wacom devices found. Retrying in %d seconds...\n", CHECK_INTERVAL);
                               sleep(CHECK_INTERVAL);
                               continue;
                           }

                           // Check if keyboard device is still valid
                           if (keyboard_fd == -1) {
                               printf("Keyboard device disconnected. Attempting to reconnect...\n");
                               keyboard_fd = find_keyboard_device(keyboard_name_pattern);
                               if (keyboard_fd == -1) {
                                   printf("Failed to reconnect keyboard device. Retrying in %d seconds...\n", CHECK_INTERVAL);
                                   for (int i = 0; i < num_wacom; i++) close(wacom_fds[i]);
                                   sleep(CHECK_INTERVAL);
                                   continue;
                               }
                           }

                           // Set up poll with keyboard device and all wacom devices
                           nfds = 0;

                           // Add keyboard device
                           fds[nfds].fd = keyboard_fd;
                           fds[nfds].events = POLLIN;
                           fds[nfds].revents = 0;
                           nfds++;

                           // Add wacom devices
                           for (int i = 0; i < num_wacom; i++) {
                               fds[nfds].fd = wacom_fds[i];
                               fds[nfds].events = POLLIN;
                               fds[nfds].revents = 0;
                               nfds++;
                           }

                           while (keep_running) {
                               ret = poll(fds, nfds, 1000);

                               if (ret == -1) {
                                   perror("poll error");
                                   break;
                               } else if (ret == 0) {
                                   // Timeout - continue polling
                                   continue;
                               }

                               bool device_error = false;
                               bool keyboard_error = false;

                               for (int i = 0; i < nfds; i++) {
                                   if (fds[i].revents & POLLIN) {
                                       while (read(fds[i].fd, &ev, sizeof(ev)) > 0) {
                                           process_event(&ev, &tip_pressed, &button_pressed, &ctrl_pressed, &z_pressed);
                                       }
                                   }
                                   if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                                       if (i == 0) { // Keyboard device is first in the array
                                           keyboard_error = true;
                                           printf("Keyboard device error detected.\n");
                                       } else {
                                           device_error = true;
                                       }
                                   }
                               }

                               if (keyboard_error) {
                                   printf("Keyboard device disconnected. Attempting to reconnect...\n");
                                   close(keyboard_fd);
                                   keyboard_fd = -1;
                                   break;
                               }

                               if (device_error) {
                                   printf("Wacom device error detected. Re-scanning Wacom devices...\n");
                                   tablet_ctrl_active = false;
                                   delayed_release_active = false;
                                   for (int i = 0; i < num_wacom; i++) close(wacom_fds[i]);
                                   break;
                               }
                           }
                       }

                       tablet_ctrl_active = false;
                       delayed_release_active = false;
                       if (keyboard_fd != -1) close(keyboard_fd);
                       for (int i = 0; i < num_wacom; i++) close(wacom_fds[i]);

                       return 0;
                   }