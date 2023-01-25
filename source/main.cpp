/**
 * DS Plasma (Soft of a wallpaper / screensaver)
 * By Brais Solla G.
 * 
 * Ported from Android NDK examples as an exercice of optimization
 * Ported to run on a Nintendo DS(i) using libnds
 */

#include <nds.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define SCREEN_WIDTH  256
#define SCREEN_HEIGHT 192

typedef int32_t  Fixed;
#define  FIXED_BITS           16
#define  FIXED_ONE            (1 << FIXED_BITS)
#define  FIXED_AVERAGE(x,y)   (((x) + (y)) >> 1)
#define  FIXED_FROM_INT(x)    ((x) << FIXED_BITS)
#define  FIXED_TO_INT(x)      ((x) >> FIXED_BITS)
#define  FIXED_FROM_FLOAT(x)  ((Fixed)((x)*FIXED_ONE))
#define  FIXED_TO_FLOAT(x)    ((x)/(1.*FIXED_ONE))
#define  FIXED_MUL(x,y)       (((int64_t)(x) * (y)) >> FIXED_BITS)
#define  FIXED_DIV(x,y)       (((int64_t)(x) * FIXED_ONE) / (y))
#define  FIXED_DIV2(x)        ((x) >> 1)
#define  FIXED_AVERAGE(x,y)   (((x) + (y)) >> 1)
#define  FIXED_FRAC(x)        ((x) & ((1 << FIXED_BITS)-1))
#define  FIXED_TRUNC(x)       ((x) & ~((1 << FIXED_BITS)-1))
#define  FIXED_FROM_INT_FLOAT(x,f)   (Fixed)((x)*(FIXED_ONE*(f)))

typedef int32_t  Angle;
#define  ANGLE_BITS              9
#if ANGLE_BITS < 8
#  error ANGLE_BITS must be at least 8
#endif
#define  ANGLE_2PI               (1 << ANGLE_BITS)
#define  ANGLE_PI                (1 << (ANGLE_BITS-1))
#define  ANGLE_PI2               (1 << (ANGLE_BITS-2))
#define  ANGLE_PI4               (1 << (ANGLE_BITS-3))
#define  ANGLE_FROM_FLOAT(x)   (Angle)((x)*ANGLE_PI/M_PI)
#define  ANGLE_TO_FLOAT(x)     ((x)*M_PI/ANGLE_PI)
#if ANGLE_BITS <= FIXED_BITS
#  define  ANGLE_FROM_FIXED(x)     (Angle)((x) >> (FIXED_BITS - ANGLE_BITS))
#  define  ANGLE_TO_FIXED(x)       (Fixed)((x) << (FIXED_BITS - ANGLE_BITS))
#else
#  define  ANGLE_FROM_FIXED(x)     (Angle)((x) << (ANGLE_BITS - FIXED_BITS))
#  define  ANGLE_TO_FIXED(x)       (Fixed)((x) >> (ANGLE_BITS - FIXED_BITS))
#endif

DTCM_DATA static Fixed angle_sin_tab[ANGLE_2PI+1];

volatile uint32_t frame = 0;

void VblankIRQ() {
	frame++;
}

static void init_angles(void){
    int  nn;
    for (nn = 0; nn < ANGLE_2PI+1; nn++) {
        double  radians = nn*M_PI/ANGLE_PI;
        angle_sin_tab[nn] = FIXED_FROM_FLOAT(sin(radians));
    }
}

static __inline__ Fixed angle_sin( Angle  a )
{
    return angle_sin_tab[(uint32_t)a & (ANGLE_2PI-1)];
}
static __inline__ Fixed angle_cos( Angle  a )
{
    return angle_sin(a + ANGLE_PI2);
}
static __inline__ Fixed fixed_sin( Fixed  f )
{
    return angle_sin(ANGLE_FROM_FIXED(f));
}
static __inline__ Fixed  fixed_cos( Fixed  f )
{
    return angle_cos(ANGLE_FROM_FIXED(f));
}
/* Color palette used for rendering the plasma */
#define  PALETTE_BITS   8
#define  PALETTE_SIZE   (1 << PALETTE_BITS)
#if PALETTE_BITS > FIXED_BITS
#  error PALETTE_BITS must be smaller than FIXED_BITS 
#endif
DTCM_DATA static uint16_t palette[PALETTE_SIZE];

static uint16_t  make565(int red, int green, int blue){
    return (uint16_t)( ((red   << 8) & 0xf800) |
                       ((green << 2) & 0x03e0) |
                       ((blue  >> 3) & 0x001f) );
}
static void init_palette(void){
    int  nn, mm = 0;
    /* fun with colors */
    for (nn = 0; nn < PALETTE_SIZE/4; nn++) {
        int  jj = (nn-mm)*4*255/PALETTE_SIZE;
        palette[nn] = make565(255, jj, 255-jj);
    }
    for ( mm = nn; nn < PALETTE_SIZE/2; nn++ ) {
        int  jj = (nn-mm)*4*255/PALETTE_SIZE;
        palette[nn] = make565(255-jj, 255, jj);
    }
    for ( mm = nn; nn < PALETTE_SIZE*3/4; nn++ ) {
        int  jj = (nn-mm)*4*255/PALETTE_SIZE;
        palette[nn] = make565(0, 255-jj, 255);
    }
    for ( mm = nn; nn < PALETTE_SIZE; nn++ ) {
        int  jj = (nn-mm)*4*255/PALETTE_SIZE;
        palette[nn] = make565(jj, 0, 255);
    }
}
static __inline__ uint16_t  palette_from_fixed(Fixed x){
    if (x < 0) x = -x;
    if (x >= FIXED_ONE) x = FIXED_ONE-1;
    int  idx = FIXED_FRAC(x) >> (FIXED_BITS - PALETTE_BITS);
    return palette[idx & (PALETTE_SIZE-1)];
}

static void init_tables(void){
    init_palette();
    init_angles();
}

#define YT1_INCR FIXED_FROM_FLOAT(1/100.)
#define YT2_INCR FIXED_FROM_FLOAT(1/163.)
#define XT1_INCR FIXED_FROM_FLOAT(1/173.)
#define XT2_INCR FIXED_FROM_FLOAT(1/242.)

// Store this function into the ARM9 ITCM (32kB 0 waitstate memory)
ITCM_CODE void drawPlasma(uint16_t* fbo, uint32_t frame){
    Fixed yt1 = FIXED_FROM_FLOAT(frame/73.82f);
    Fixed yt2 = yt1;
    Fixed xt10 = FIXED_FROM_FLOAT(frame/180.f);
    Fixed xt20 = xt10;
    
    void* pixels = (void*) fbo;
    int yy;
    for(yy = 0; yy < SCREEN_HEIGHT; yy++){
        uint16_t* line = (uint16_t*) pixels;
        Fixed     base = fixed_sin(yt1) + fixed_sin(yt2);
        Fixed     xt1  = xt10;
        Fixed     xt2  = xt20;
        
        yt1 += YT1_INCR;
        yt2 += YT2_INCR;
        
        uint16_t* line_end = line + SCREEN_WIDTH;
        if(line < line_end){
            if(((uint32_t) line & 3) != 0){
                Fixed ii = base + fixed_sin(xt1) + fixed_sin(xt2);
                
                xt1 += XT1_INCR;
                xt2 += XT2_INCR;
                
                *line = palette_from_fixed(ii >> 2);
                line++;
            }
            while(line + 2 <= line_end){
                Fixed i1 = base + fixed_sin(xt1) + fixed_sin(xt2);
                xt1 += XT1_INCR;
                xt2 += XT2_INCR;
             
                Fixed i2 = base + fixed_sin(xt1) + fixed_sin(xt2);
                xt1 += XT1_INCR;
                xt2 += XT2_INCR;
             
                uint32_t pixel = ((uint32_t) palette_from_fixed(i1 >> 2) << 16) | (uint32_t) palette_from_fixed(i2 >> 2);
                ((uint32_t*)line)[0] = pixel;
                line += 2;
            }
            
            if(line < line_end) {
                Fixed ii = base + fixed_sin(xt1) + fixed_sin(xt2);
                *line    = palette_from_fixed(ii >> 2);
                line++;
            }
        }
        pixels = (uint16_t*) pixels + 256; // Stride 0?
    }
    return;
}

#ifndef SCFG_EXT9
// Hardware registers to enable 32 bit bus width and 8 bit writes to the DSi VRAM (Only DSi on TWL mode!)
#define TWL_BASE_ADDR (0x4004000)
#define SCFG_EXT9_ADDR (TWL_BASE_ADDR + 0x8)
#define SCFG_EXT9_REG *((volatile uint32_t*) SCFG_EXT9_ADDR)

#define SCFG_EXT9_REVISED_DMA          (1 << 0)
#define SCFG_EXT9_EXTENDED_VRAM_ACCESS (1 << 13)

#endif

int main(void) {
    consoleDemoInit();
    
    // Use VRAM A as background 3 in the main engine, framebuffer like mode
    vramSetBankA(VRAM_A_MAIN_BG);
    videoSetMode(MODE_5_2D);
    int bg3Main = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 1, 0);
    uint16_t* fbo = bgGetGfxPtr(bg3Main);

	iprintf("[DS Plasma]\n");
	iprintf("by Brais Solla G.\n");
	iprintf("Based on NDK Example plasma\n");
    
    if(isDSiMode()){
        // DSi in TWL mode, push CPU to 134 MHz, enable extended access to memory
        setCpuClock(true);
        SCFG_EXT9_REG |= (SCFG_EXT9_EXTENDED_VRAM_ACCESS | SCFG_EXT9_REVISED_DMA);
        
        iprintf("\nTWL Mode (134 MHz)\n");
    } else {
        iprintf("\nNTR Mode (67 MHz)\n");
    }
    
    // 16 bpp, clear framebuffer
    memset(fbo, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    iprintf("\n");
    iprintf("Generating LUTs...\n");
    // Init plasma...
    
    cpuStartTiming(0);
    // !: We can probably optimize this more using the NDS hardware palettes! 
    // Check: http://problemkaputt.de/gbatek.htm#dsvideobgmodescontrol // http://problemkaputt.de/gbatek.htm#dsvideoextendedpalettes
    init_tables();
    uint32_t ticks = cpuEndTiming();
    printf("LUTS done! (%.3f ms)\n", (float) (ticks / (float) BUS_CLOCK) * 1000.f);
    printf("%lu ticks (%lu CLK)\n\n", (uint32_t) ticks, (uint32_t) BUS_CLOCK);
    
    iprintf("Running plasma!\n");
    swiWaitForVBlank();
    
    irqSet(IRQ_VBLANK, VblankIRQ);
    
    while(1){
        // Red color on background backdrop (0) to check CPU usage graphically
        BG_PALETTE_SUB[0] = 31;
        scanKeys();
        int keys = keysDown();
        if (keys & KEY_START) break;
        
        cpuStartTiming(0);
        drawPlasma(fbo, frame);
        uint32_t plasma_ticks = cpuEndTiming();
        
        
        iprintf("\x1b[15;0HFrame = %lu",frame);
        // TODO: Avoid using floating point operations on the DS CPU! It does not have a FPU! (ARM946E-S)
        printf("\x1b[16;0HPlasma= %.2f ms", (float) plasma_ticks / (float) BUS_CLOCK * 1000.f);

        
        // Set backdrop color to black
        BG_PALETTE_SUB[0] = 0x0000;
        swiWaitForVBlank();
    }
    return 0;
}
