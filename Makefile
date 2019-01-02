PRGNAME     = vbam.elf

# define regarding OS, which compiler to use
CC          = gcc
CCP         = g++
LD          = gcc

# change compilation / linking flag options
CFLAGS		= -DSDL -DNO_FILTERS -DC_CORE -DNO_ASM -DNDEBUG -DNO_FFMPEG -DHAVE_NETINET_IN_H -DHAVE_ARPA_INET_H -DHAVE_ZLIB_H -DFINAL_VERSION -DNO_LINK -DNO_DEBUGGER -DLOW_END
CFLAGS 		+= -DPACKAGE="" -I/usr/include/SDL -DSYSCONF_INSTALL_DIR=\"/switch\" -DPKGDATADIR=\"/switch/vbam\"
CFLAGS		+= -DUSE_TWEAK_SPEEDHACK
CFLAGS		+= -Ofast -fdata-sections -ffunction-sections
CFLAGS		+= -I./src/apu -I./src/art -I./src/gb -I./src/gba -I./src/sdl -I./src -I. -Isrc/common -I. -Ifex
CXXFLAGS	= $(CFLAGS) -fpermissive

LDFLAGS     = -lSDL -lm -Wl,--as-needed -Wl,--gc-sections -flto -lstdc++ -lpng16 -lz

# Files to be compiled
SRCDIR    = ./src/apu ./src/art ./src/gb ./src/gba ./src/sdl ./src/common ./src ./fex/fex ./fex/7z_C
VPATH     = $(SRCDIR)
SRC_C   = $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
SRC_CP   = $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.cpp))
OBJ_C   = $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJ_CP   = $(notdir $(patsubst %.cpp, %.o, $(SRC_CP)))
OBJS     = $(OBJ_C) $(OBJ_CP)

# Rules to make executable
$(PRGNAME): $(OBJS)  
	$(LD) $(CFLAGS) -o $(PRGNAME) $^ $(LDFLAGS)

$(OBJ_C) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_CP) : %.o : %.cpp
	$(CCP) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(PRGNAME)$(EXESUFFIX) *.o
