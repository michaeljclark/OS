// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include <stdarg.h>

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "acpi.h"
#include "vga.h"
#include "vga_modes.h"

static void consputc(int, uint32);
static void vga_putc(unsigned char c, unsigned char forecolour, unsigned char backcolour, int x, int y);

#define CGA_BLACK         0x0
#define CGA_BLUE          0x1
#define CGA_GREEN         0x2
#define CGA_CYAN          0x3
#define CGA_RED           0x4
#define CGA_MAGENTA       0x5
#define CGA_BROWN         0x6
#define CGA_LIGHT_GRAY    0x7
#define CGA_DARK_GRAY     0x8
#define CGA_LIGHT_BLUE    0x9
#define CGA_LIGHT_GREEN   0xA
#define CGA_LIGHT_CYAN    0xB
#define CGA_LIGHT_RED     0xC
#define CGA_LIGHT_MAGENTA 0xD
#define CGA_YELLOW        0xE
#define CGA_WHITE         0xF

#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_MAGENTA       0x5
#define VGA_BROWN         0x14
#define VGA_LIGHT_GRAY    0x7
#define VGA_DARK_GRAY     0x38
#define VGA_LIGHT_BLUE    0x39
#define VGA_LIGHT_GREEN   0x3A
#define VGA_LIGHT_CYAN    0x3B
#define VGA_LIGHT_RED     0x3C
#define VGA_LIGHT_MAGENTA 0x3D
#define VGA_YELLOW        0x3E
#define VGA_WHITE         0x3F

#define CGA_FONT_COLOR(foreground, background) ((background << 4) + foreground)
#define DEFAULT_CONSOLE_COLOR CGA_FONT_COLOR(VGA_LIGHT_GRAY, VGA_BLACK)
#define CGA_GET_FONT_BACKGROUND_COLOR(color) ((color & 0xFF00) >> 4)

#define COLUMNS 80

static uint8 g_80x25_text[] = VGA_80X25_TEXT_MODE;
static uint8 g_8x16_font[4096] = VGA_8X16_FONT;

static int panicked = 0;

static struct {
    struct spinlock lock;
    int locking;
} cons;

static char digits[] = "0123456789abcdef";

static void printptr(uintp x, uint32 color) {
    int i;
    for (i = 0; i < (sizeof(uintp) * 2); i++, x <<= 4)
        consputc(digits[x >> (sizeof(uintp) * 8 - 4)], color);
}

static void printint(int xx, int base, int sign, uint32 color){
    char buf[16];
    int i;
    uint x;

    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    i = 0;
    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0)
        consputc(buf[i], color);
}

void cprintf(char* fmt, ...){
    va_list ap;
    int i, c, locking;
    char* s;

    va_start(ap, fmt);

    locking = cons.locking;
    if (locking)
        acquire(&cons.lock);

    if (fmt == 0)
        panic("null fmt");

    uint32 color = DEFAULT_CONSOLE_COLOR;
    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            consputc(c, color);
            continue;
        }
        c = fmt[++i] & 0xff;
        if (c == 0)
            break;
        switch (c) {
        case 'd':
            printint(va_arg(ap, int), 10, 1, color);
            break;
        case 'x':
            printint(va_arg(ap, int), 16, 0, color);
            break;
        case 'p':
            printptr(va_arg(ap, uintp), color);
            break;
        case 's':
            if ((s = va_arg(ap, char*)) == 0)
                s = "(null)";
            for (; *s; s++)
                consputc(*s, color);
            break;
        case '%':
            consputc('%', color);
            break;
        default:
            // Print unknown % sequence to draw attention.
            consputc('%', color);
            consputc(c, color);
            break;
        }
    }

    if (locking)
        release(&cons.lock);
}

void panic(char* s){
    uintp pcs[10];

    cli();
    cons.locking = 0;
    cprintf("\n\nPANIC on cpu %d\n ", cpu->id);
    cprintf(s);
    cprintf("\nSTACK:\n");
    getcallerpcs(&s, pcs);
    for (int i = 0; i < 10 && pcs[i] != 0x0; i++){
        cprintf(" [%d] %p\n",i, pcs[i]);
    }
    cprintf("HLT\n");
    panicked = 1; // freeze other CPU
    acpi_halt();
    for (;;)
        ;
}

#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static uint16* crt = (uint16*)P2V(VGA_TEXT_MEM);

static void console_setbackgroundcolor(uint32 color){
  memset(crt, color, 0xFA00);
}

static void cgaputc(int c, uint32 color){
    int pos;

    // Cursor position: col + 80*row.
    outb(CRTPORT, 14);
    pos = inb(CRTPORT + 1) << 8;
    outb(CRTPORT, 15);
    pos |= inb(CRTPORT + 1);

    if (c == '\n')
        pos += COLUMNS - pos % COLUMNS;
    else if (c == BACKSPACE) {
        if (pos > 0) --pos;
    } else
        crt[pos++] = (c & 0xFF) | (color << 8);

    if ((pos / COLUMNS) >= 24) { // Scroll up.
        memmove(crt, crt + COLUMNS, sizeof(crt[0]) * 23 * COLUMNS);
        pos -= COLUMNS;
        memset(crt + pos, 0, sizeof(crt[0]) * (24 * COLUMNS - pos));
    }

    outb(CRTPORT, 14);
    outb(CRTPORT + 1, pos >> 8);
    outb(CRTPORT, 15);
    outb(CRTPORT + 1, pos);
    crt[pos] = ' ' | 0x0700;
}

static void consputc(int c, uint32 color){
    if (panicked) {
        cli();
        for (;;)
            ;
    }

    if (c == BACKSPACE) {
        uartputc('\b'); uartputc(' '); uartputc('\b');
    } else
        uartputc(c);
    cgaputc(c, color);
}

#define INPUT_BUF 128
struct {
    struct spinlock lock;
    char buf[INPUT_BUF];
    uint r; // Read index
    uint w; // Write index
    uint e; // Edit index
} input;

#define C(x)  ((x) - '@')  // Control-x

void consoleintr(int (*getc)(void)){
    int c;

    acquire(&input.lock);
    while ((c = getc()) >= 0) {
        switch (c) {
        case C('Z'): // reboot
            lidt(0, 0);
            break;
        case C('P'): // Process listing.
            procdump();
            break;
        case C('U'): // Kill line.
            while (input.e != input.w &&
                   input.buf[(input.e - 1) % INPUT_BUF] != '\n') {
                input.e--;
                consputc(BACKSPACE, DEFAULT_CONSOLE_COLOR);
            }
            break;
        case C('H'): case '\x7f': // Backspace
            if (input.e != input.w) {
                input.e--;
                consputc(BACKSPACE, DEFAULT_CONSOLE_COLOR);
            }
            break;
        default:
            if (c != 0 && input.e - input.r < INPUT_BUF) {
                c = (c == '\r') ? '\n' : c;
                input.buf[input.e++ % INPUT_BUF] = c;
                consputc(c, DEFAULT_CONSOLE_COLOR);
                if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF) {
                    input.w = input.e;
                    wakeup(&input.r);
                }
            }
            break;
        }
    }
    release(&input.lock);
}

int consoleread(struct inode* ip, char* dst, int n){
    uint target;
    int c;

    iunlock(ip);
    target = n;
    acquire(&input.lock);
    while (n > 0) {
        while (input.r == input.w) {
            if (proc->killed) {
                release(&input.lock);
                ilock(ip);
                return -1;
            }
            sleep(&input.r, &input.lock);
        }
        c = input.buf[input.r++ % INPUT_BUF];
        if (c == C('D')) { // EOF
            if (n < target) {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                input.r--;
            }
            break;
        }
        *dst++ = c;
        --n;
        if (c == '\n')
            break;
    }
    release(&input.lock);
    ilock(ip);

    return target - n;
}

int consolewrite(struct inode* ip, char* buf, int n){
    int i;

    iunlock(ip);
    acquire(&cons.lock);
    for (i = 0; i < n; i++)
        consputc(buf[i] & 0xff, DEFAULT_CONSOLE_COLOR);
    release(&cons.lock);
    ilock(ip);

    return n;
}


static void vga_init(){
  vga_write_regs(g_80x25_text);


  for (uint16 i = 0; i < 4096; i += 16) {
      for (uint16 j = 0; j < 16; j++) {
          ((char *) KERNBASE + 0xa0000)[2*i+j] = g_8x16_font[i+j];
      }
  }
}

void consoleinit(void){
    initlock(&cons.lock, "console");
    initlock(&input.lock, "input");

    devsw[CONSOLE].write = consolewrite;
    devsw[CONSOLE].read = consoleread;
    cons.locking = 1;

    picenable(IRQ_KBD);
    ioapicenable(IRQ_KBD, 0);

    vga_init();
    console_setbackgroundcolor(VGA_BLACK);

    uint32 backColor = CGA_GET_FONT_BACKGROUND_COLOR(DEFAULT_CONSOLE_COLOR);
    cprintf("VGA ");
    consputc('C', CGA_FONT_COLOR(VGA_RED, backColor));
    consputc('O', CGA_FONT_COLOR(VGA_MAGENTA, backColor));
    consputc('L', CGA_FONT_COLOR(VGA_LIGHT_GREEN, backColor));
    consputc('O', CGA_FONT_COLOR(VGA_YELLOW, backColor));
    consputc('R', CGA_FONT_COLOR(VGA_GREEN, backColor));
    cprintf(" Console\n");
}
