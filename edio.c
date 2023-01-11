// Based on https://github.com/krikzz/EDN8-PRO/blob/master/edlink-n8/edlink-n8/Edio.cs
// Compile: clang -std=c99 edio.c -o edio

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

const uint8_t expectedStatus = 0xa5;
const uint8_t serviceMode = 0xa1;
const uint8_t appMode = 0xa2;
const uint8_t fatWrite = 2;
const uint8_t fatCreateAlways = 8;
const uint8_t dirExists = 8;

void reportIOError(const char * message, ssize_t actual, ssize_t expected) {
    fprintf(stderr, "%s: %ld vs %ld\n", message, actual, expected);
    if (errno) {
        fprintf(stderr, "\terror %d: %s\n", errno, strerror(errno));
    }
}

uint8_t checkStatus(int fd, uint8_t silence) {
    uint8_t code = 0x10;
    uint8_t cmd[4] = { '+', '+' ^ 0xff, code, code ^ 0xff };
    ssize_t w = write(fd, cmd, 4 * sizeof(uint8_t));

    if (w != 4 * sizeof(uint8_t)) {
        reportIOError("checkStatus write failure", w, 4 * sizeof(uint8_t));
        return 0xff;
    }

    uint8_t response[2] = { 0xff, 0xff };
    ssize_t r = read(fd, &response, 2 * sizeof(uint8_t));

    if (r != 2 * sizeof(uint8_t)) {
        reportIOError("checkStatus read failure", r, 2 * sizeof(uint8_t));
        return 0xff;
    }

    if (response[1] != expectedStatus || (response[0] && response[0] != silence)) {
        fprintf(stderr, "bad status: 0x%02x 0x%02x\n", response[1], response[0]);
        return response[0];
    }

    return 0;
}

uint8_t getMode(int fd) {
    uint8_t code = 0x11;
    uint8_t cmd[4] = { '+', '+' ^ 0xff, code, code ^ 0xff };
    ssize_t w = write(fd, cmd, 4 * sizeof(uint8_t));

    if (w != 4 * sizeof(uint8_t)) {
        reportIOError("getMode write failure", w, 4 * sizeof(uint8_t));
        return 0xff;
    }

    uint8_t response = 0xff;
    ssize_t r = read(fd, &response, sizeof(uint8_t));

    if (r != sizeof(uint8_t)) {
        reportIOError("getMode read failure", r, sizeof(uint8_t));
        return 0xff;
    }

    if (response != serviceMode && response != appMode) {
        fprintf(stderr, "bad mode: 0x%02x\n", response);
    }

    return response;
}

void runApp(int * fd, const char * port, struct termios * backup, struct termios * tty) {
    uint8_t mode = getMode(*fd);
    if (mode == appMode) {
        return;
    }

    uint8_t code = 0xf1;
    uint8_t cmd[4] = { '+', '+' ^ 0xff, code, code ^ 0xff };
    ssize_t w = write(*fd, cmd, 4 * sizeof(uint8_t));

    if (w != 4 * sizeof(uint8_t)) {
        reportIOError("runApp write failure", w, 4 * sizeof(uint8_t));

        tcsetattr(*fd, TCSANOW, backup);
        close(*fd);
        *fd = -1;
        return;
    }

    tcsetattr(*fd, TCSANOW, backup);
    close(*fd);

    for (int retry = 0; retry < 4; ++retry) {
        sleep(1);
        *fd = open(port, O_RDWR);
        tcgetattr(*fd, backup);
        tcsetattr(*fd, TCSANOW, tty);

        if (!checkStatus(*fd, 0) && getMode(*fd) == appMode) {
            return;
        }

        tcsetattr(*fd, TCSANOW, backup);
        close(*fd);
    }

    fprintf(stderr, "runApp failed to reconnect\n");
    *fd = -1;
}

uint8_t diskInit(int fd) {
    uint8_t code = 0xc0;
    uint8_t cmd[4] = { '+', '+' ^ 0xff, code, code ^ 0xff };
    ssize_t w = write(fd, cmd, 4 * sizeof(uint8_t));

    if (w != 4 * sizeof(uint8_t)) {
        reportIOError("diskInit write failure", w, 4 * sizeof(uint8_t));
        return 0xff;
    }

    return checkStatus(fd, 0);
}

uint8_t dirMake(int fd, const char * path) {
    uint8_t code = 0xd2;
    uint8_t cmd[4] = { '+', '+' ^ 0xff, code, code ^ 0xff };
    ssize_t w = write(fd, cmd, 4 * sizeof(uint8_t));

    if (w != 4 * sizeof(uint8_t)) {
        reportIOError("dirMake command write failure", w, 4 * sizeof(uint8_t));
        return 0xff;
    }

    uint16_t pathLength = strlen(path);
    w = write(fd, &pathLength, sizeof(uint16_t));

    if (w != sizeof(uint16_t)) {
        reportIOError("dirMake pathLength write failure", w, sizeof(uint16_t));
        return 0xff;
    }

    w = write(fd, path, pathLength * sizeof(char));

    if (w != pathLength * sizeof(char)) {
        reportIOError("dirMake path write failure", w, (ssize_t )(pathLength * sizeof(char)));
        return 0xff;
    }

    return checkStatus(fd, dirExists);
}

uint8_t hierarchyMake(int fd, const char * dst) {
    size_t size = strlen(dst) + 1;
    char * path = malloc(size * sizeof(char));
    strcpy(path, dst);

    char * p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';

            uint8_t result = dirMake(fd, path);
            if (result) {
                free(path);
                return result;
            }
            *p = c;
        }
        ++p;
    }

    free(path);
    return 0;
}

uint8_t fileOpen(int fd, const char * dst) {
    uint8_t code = 0xc9;
    uint8_t cmd[4] = { '+', '+' ^ 0xff, code, code ^ 0xff };
    ssize_t w = write(fd, cmd, 4 * sizeof(uint8_t));

    if (w != 4 * sizeof(uint8_t)) {
        reportIOError("fileOpen command write failure", w, 4 * sizeof(uint8_t));
        return 0xff;
    }

    uint8_t mode = fatWrite | fatCreateAlways;
    w = write(fd, &mode, sizeof(uint8_t));

    if (w != sizeof(uint8_t)) {
        reportIOError("fileOpen mode write failure", w, sizeof(uint8_t));
        return 0xff;
    }

    uint16_t pathLength = strlen(dst);
    w = write(fd, &pathLength, sizeof(uint16_t));

    if (w != sizeof(uint16_t)) {
        reportIOError("fileOpen pathLength write failure", w, sizeof(uint16_t));
        return 0xff;
    }

    w = write(fd, dst, pathLength * sizeof(char));

    if (w != pathLength * sizeof(char)) {
        reportIOError("fileOpen path write failure", w, (ssize_t)(pathLength * sizeof(char)));
        return 0xff;
    }

    return checkStatus(fd, 0);
}

uint8_t fileWrite(int fd, const char * src) {
    FILE * file = fopen(src, "rb");
    if (!file) {
        fprintf(stderr, "fileWrite failed to open source file: %s\n", src);
        return 0xff;
    }

    fseek(file, 0, SEEK_END);
    uint32_t romLength = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t * rom = malloc(romLength * sizeof(uint8_t));
    unsigned long fr = fread(rom, sizeof(uint8_t), romLength, file);
    fclose(file);

    if (fr != romLength * sizeof(uint8_t)) {
        fprintf(stderr, "fileWrite failed to read source file: %s\n", src);
        free(rom);
        return 0xff;
    }

    uint8_t code = 0xcc;
    uint8_t cmd[4] = { '+', '+' ^ 0xff, code, code ^ 0xff };
    ssize_t w = write(fd, cmd, 4 * sizeof(uint8_t));

    if (w != 4 * sizeof(uint8_t)) {
        reportIOError("fileWrite command write failure", w, 4 * sizeof(uint8_t));
        free(rom);
        return 0xff;
    }

    w = write(fd, &romLength, sizeof(uint32_t));

    if (w != sizeof(uint32_t)) {
        reportIOError("fileWrite romLength write failure", w, sizeof(uint32_t));
        free(rom);
        return 0xff;
    }

    uint32_t remaining = romLength;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint8_t response = 0xff;
        ssize_t r = read(fd, &response, sizeof(uint8_t));

        if (r != sizeof(uint8_t)) {
            reportIOError("fileWrite response read failure", r, sizeof(uint8_t));
            free(rom);
            return 0xff;
        }

        if (response) {
            fprintf(stderr, "fileWrite bad response: 0x%02x\n", response);
            free(rom);
            return 0xff;
        }

        uint32_t block = 1024;
        if (block > remaining) {
            block = remaining;
        }

        w = write(fd, rom + offset, block * sizeof(uint8_t));

        if (w < 0) {
            reportIOError("fileWrite data write failure", w, (ssize_t)(block * sizeof(uint8_t)));
            free(rom);
            return 0xff;
        }

        remaining -= w;
        offset += w;
    }

    free(rom);
    return checkStatus(fd, 0);
}

uint8_t fileClose(int fd) {
    uint8_t code = 0xce;
    uint8_t cmd[4] = { '+', '+' ^ 0xff, code, code ^ 0xff };
    ssize_t w = write(fd, cmd, 4 * sizeof(uint8_t));

    if (w != 4 * sizeof(uint8_t)) {
        reportIOError("fileClose write failure", w, 4 * sizeof(uint8_t));
        return 0xff;
    }

    return checkStatus(fd, 0);
}

int main(int argc, char ** argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: %s <local/path/to/src.nes> <remote/path/to/dst.nes> [/dev/path_to_everdrive]", argv[0]);
        return 1;
    }

    const char * src = argv[1];
    const char * dst = argv[2];

    const char * port = "/dev/cu.usbmodem00000000001A1";
    if (argc == 4) {
        port = argv[3];
    }

    printf("opening connection to Everdrive...\n");

    int fd = open(port, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "failed to open serial port (%d): %s\n", errno, strerror(errno));
        return 1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "failed to retrieve tty settings (%d): %s\n", errno, strerror(errno));
        close(fd);
        return 1;
    }

    struct termios backup = tty;

    cfsetspeed(&tty, B9600);
    cfmakeraw(&tty);

    tty.c_cc[VTIME] = 10;
    tty.c_cc[VMIN] = 0;

    tty.c_cflag &= ~(CSTOPB | CRTSCTS);
    tty.c_cflag |= CREAD | CLOCAL;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "failed to set tty settings (%d): %s\n", errno, strerror(errno));
        close(fd);
        return 1;
    }

    printf("switching to application mode (if necessary)...\n");

    runApp(&fd, port, &backup, &tty);
    if (fd < 0) {
        return 1;
    }

    printf("initializing disk...\n");

    if (diskInit(fd)) {
        tcsetattr(fd, TCSANOW, &backup);
        close(fd);
        return 1;
    }

    printf("creating directories (if necessary)...\n");

    if (hierarchyMake(fd, dst)) {
        tcsetattr(fd, TCSANOW, &backup);
        close(fd);
        return 1;
    }

    printf("writing rom file...\n");
    
    fileOpen(fd, dst);
    fileWrite(fd, src);
    fileClose(fd);

    tcsetattr(fd, TCSANOW, &backup);
    close(fd);

    printf("done!\n");
    return 0;
}