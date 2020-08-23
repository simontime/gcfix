#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define BANK_LENGTH       0x2000
#define BANK_LOAD_ADDRESS 0x6000

typedef struct
{
    uint8_t  size;
    uint8_t  entryBank;
    uint16_t entryAddress;
    uint8_t  unk;
    char     system[9];
    uint8_t  iconBank;
    uint8_t  iconX;
    uint8_t  iconY;
    char     title[9];
    uint8_t  gameId[2];
    uint8_t  securityCode;
    uint8_t  pad[3];
} romHeader;

// 8-bit bank, 16-bit offset x 3 x 16
unsigned char securityTable[16][9] =
{
    { 0x01, 0x73, 0xE4, 0x02, 0x77, 0x57, 0x03, 0x66, 0x66 },
    { 0x00, 0x72, 0x45, 0x01, 0x75, 0x05, 0x02, 0x67, 0x07 },
    { 0x01, 0x62, 0x67, 0x03, 0x63, 0x5A, 0x03, 0x7A, 0xBC },
    { 0x00, 0x7A, 0xC2, 0x01, 0x76, 0xBB, 0x04, 0x64, 0xE3 },
    { 0x02, 0x6F, 0x27, 0x02, 0x76, 0xE1, 0x03, 0x7F, 0xDB },
    { 0x00, 0x68, 0xA7, 0x03, 0x6B, 0x41, 0x02, 0x76, 0x73 },
    { 0x00, 0x62, 0x45, 0x01, 0x73, 0xBE, 0x04, 0x6B, 0x6F },
    { 0x00, 0x77, 0x43, 0x02, 0x7F, 0x7E, 0x03, 0x63, 0x76 },
    { 0x01, 0x68, 0x75, 0x01, 0x77, 0x64, 0x02, 0x6F, 0xD0 },
    { 0x01, 0x63, 0x0F, 0x02, 0x64, 0xE7, 0x03, 0x67, 0xB1 },
    { 0x01, 0x62, 0x09, 0x01, 0x74, 0xF1, 0x01, 0x7A, 0xA8 },
    { 0x01, 0x60, 0x0D, 0x01, 0x73, 0xC9, 0x03, 0x63, 0xEC },
    { 0x01, 0x79, 0xA7, 0x02, 0x7F, 0x4B, 0x03, 0x60, 0x78 },
    { 0x00, 0x73, 0x27, 0x01, 0x62, 0x4C, 0x03, 0x70, 0x86 },
    { 0x01, 0x69, 0x03, 0x02, 0x6F, 0x72, 0x03, 0x66, 0x00 },
    { 0x00, 0x71, 0x08, 0x01, 0x7A, 0xBB, 0x02, 0x79, 0x0A }
};

bool securityCheck(uint8_t *rom)
{
    uint8_t accum, i;
    uint8_t *tabPtr;
    uint32_t offset;
    romHeader *hdr;
    
    // skip to ROM header
    if (*rom == 0 || *rom == 0xff)
        rom += 0x40000;
    
    hdr = (romHeader *)rom;
    
    // ensure sum of game id matches security code
    if (((hdr->gameId[0] + hdr->gameId[1]) ^ 0xa5) != hdr->securityCode)
        return false;
    
    // ensure system marker is "TigerDMGC"
    if (strncmp(hdr->system, "TigerDMGC", 9) != 0)
        return false;
    
    // ensure bottom bit of unknown value is set
    if (hdr->unk & 1 == 0)
        return false;
    
    // security table bullshit
    tabPtr = securityTable[hdr->securityCode & 0xf];
    
    // tiger electronics says fuck you
    for (accum = 0, i = 0; i < 3; i++)
    {
        offset  = (tabPtr[0] * BANK_LENGTH) + (((tabPtr[1] << 8) | tabPtr[2]) - BANK_LOAD_ADDRESS);
        accum  += rom[offset];
        tabPtr += 3;
    }
    
    // this is a really stupid protection scheme
    return accum == 0x5a;
}

bool securityFix(uint8_t *rom, uint32_t length)
{
    uint8_t accum, i, j, match;
    uint8_t *tabPtr;
    uint32_t offset;
    romHeader *hdr;
    
    // ROMs shouldn't be under 32KB
    if (length < 0x8000)
    {
        fputs("Error: ROM too small!\n", stderr);
        return false;
    }
    
    // skip to ROM header
    if ((*rom == 0 || *rom == 0xff) && length > 0x8000)
        rom += 0x40000;
    
    hdr = (romHeader *)rom;
    
    // try to find a match within securityTable
    for (i = 0, match = 0xff; i < 16; i++)
    {
        accum = 0;
        
        if (length == 0x8000)
        {
            // checks past 0x8000, skip these indices
            if (i == 3 || i == 6)
                i++;
        }
        
        tabPtr = securityTable[i];
        
        for (j = 0; j < 3; j++)
        {
            offset  = (tabPtr[0] * BANK_LENGTH) + (((tabPtr[1] << 8) | tabPtr[2]) - BANK_LOAD_ADDRESS);
            accum  += rom[offset];
            tabPtr += 3;
        }
        
        // found one!
        if (accum == 0x5a)
        {
            match = i;
            break;
        }
    }
    
    if (match == 0xff)
    {
        fputs("Error: No checksum match found!\n"
              "You'll need to modify other bytes in the ROM.\n", stderr);
        return false;
    }
    
    hdr->gameId[0]    = match ^ 0xa5;
    hdr->gameId[1]    = 0;
    hdr->securityCode = match;
    
    return true;
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *rom;
    uint32_t len;
    bool check;
    
    if (argc != 3)
        goto printUsage;
    
    if ((f = fopen(argv[2], "rb+")) == NULL)
    {
        perror("Error opening ROM");
        return 1;
    }
    
    if      (strcmp(argv[1], "check") == 0) check = true;
    else if (strcmp(argv[1], "fix")   == 0) check = false;
    else goto printUsage;
    
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);
    
    rom = malloc(len);
    fread(rom, 1, len, f);
    rewind(f);
    
    if (check)
    {
        printf("Is ROM valid? %s\n", securityCheck(rom) ? "Yes" : "No");
    }
    else
    {
        if (securityFix(rom, len))
        {
            fwrite(rom, 1, len, f);
            puts("Fixed ROM security header!");
        }
        else
        {
            goto fail;
        }
    }
    
    puts("Done!");
    
fail:
    fclose(f);
    free(rom);
    
    return 0;
    
printUsage:
    printf("Usage: %s [check, fix] rom.bin\n", argv[0]);
    return 0;
}