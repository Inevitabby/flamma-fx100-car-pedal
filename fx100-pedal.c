/*
 * Turns my sibling's FLAMMA Portable Multi Effects Pedal FX100 into a shitty virtual joystick for BeamNG.drive.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define VID        0x0483
#define PID        0x5703
#define PEDAL_BYTE 10
#define RAW_MAX    50
#define AXIS_MAX   32767

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

/* Open the pedal's hidraw node by VID/PID */
static int open_pedal(void) {
    char want[48];
    snprintf(want, sizeof want, "HID_ID=0003:%08X:%08X", VID, PID);

    DIR *d = opendir("/sys/class/hidraw");
    if (!d) return -1;
    int fd = -1;
    struct dirent *e;
    while (fd < 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "hidraw", 6)) continue;
        char path[320], line[256];
        snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int match = 0;
        while (!match && fgets(line, sizeof line, f)) match = strcasestr(line, want) != NULL;
        fclose(f);
        if (!match) continue;
        snprintf(path, sizeof path, "/dev/%s", e->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) perror(path);
    }
    closedir(d);
    return fd;
}

static int uin = -1;

static void send_event(int type, int code, int value) {
    struct input_event ev = { .type = type, .code = code, .value = value };
    if (write(uin, &ev, sizeof ev) < 0) perror("uinput write");
}

static void set_axis(int code, int value) {
    send_event(EV_ABS, code, value);
    send_event(EV_SYN, SYN_REPORT, 0);
}

static int create_joystick(void) {
    uin = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uin < 0) { perror("/dev/uinput"); return -1; }

    ioctl(uin, UI_SET_EVBIT, EV_KEY);
    ioctl(uin, UI_SET_KEYBIT, BTN_TRIGGER);
    ioctl(uin, UI_SET_EVBIT, EV_ABS);
    static const int axes[] = { ABS_X, ABS_Y, ABS_Z };
    for (int i = 0; i < 3; i++) {
        struct uinput_abs_setup a = { .code = axes[i], .absinfo = { .maximum = AXIS_MAX } };
        ioctl(uin, UI_SET_ABSBIT, axes[i]);
        ioctl(uin, UI_ABS_SETUP, &a);
    }

    struct uinput_setup s = { .id = { BUS_USB, 0x1209, 0x0001, 1 }, .name = "FLAMMA Portable Multi Effects Pedal FX100" };
    if (ioctl(uin, UI_DEV_SETUP, &s) < 0 || ioctl(uin, UI_DEV_CREATE) < 0) {
        perror("uinput create");
        return -1;
    }
    set_axis(ABS_X, AXIS_MAX / 2); /* center unused axes */
    set_axis(ABS_Y, AXIS_MAX / 2);
    set_axis(ABS_Z, 0);
    return 0;
}

static int scale(int raw) {
    int v = raw * AXIS_MAX / RAW_MAX;
    if (v > AXIS_MAX) v = AXIS_MAX;
    if (v < AXIS_MAX / 50) v = 0; /* 2% bottom deadzone */
    if (v > AXIS_MAX - AXIS_MAX / 50) v = AXIS_MAX; /* 2% top deadzone */
    return v;
}

int main(void) {
    struct sigaction sa = { .sa_handler = on_signal }; /* exit poll() on ctrl+c */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (create_joystick() < 0) return 1;
    fprintf(stderr, "virtual pedal created; Ctrl-C to quit\n");

    int hid = -1;
    while (running) {
        if (hid < 0) {
            hid = open_pedal();
            if (hid < 0) { poll(NULL, 0, 1000); continue; }
            fprintf(stderr, "pedal connected\n");
        }

        /* keep the last value */
        struct pollfd p = { .fd = hid, .events = POLLIN };
        if (poll(&p, 1, 500) <= 0 && !(p.revents & (POLLERR | POLLHUP))) continue;

        unsigned char buf[64];
        ssize_t n;
        int raw = -1;
        while ((n = read(hid, buf, sizeof buf)) > PEDAL_BYTE) raw = buf[PEDAL_BYTE]; /* keep newest */
        if (raw >= 0) set_axis(ABS_Z, scale(raw));

        if (n == 0 || (p.revents & (POLLERR | POLLHUP))) { /* pedal unplugged */
            fprintf(stderr, "pedal disconnected\n");
            close(hid);
            hid = -1;
            set_axis(ABS_Z, 0); /* fail safe: no throttle */
        }
    }

    set_axis(ABS_Z, 0);
    ioctl(uin, UI_DEV_DESTROY);
    return 0;
}
