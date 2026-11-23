TARGET = SobrevivendoEmGS
OBJS = main.o

INCDIR =
CFLAGS = -O2 -G0 -Wall -ffast-math
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LIBS = -lpspgum -lpspgu -lpspdebug -lpspdisplay -lpspge -lpspctrl -lm

BUILD_PRX = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Sobrevivendo Em GS
PSP_EBOOT_ICON = ICON0.PNG
PSP_FW_VERSION = 500

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
