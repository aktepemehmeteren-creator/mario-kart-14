TARGET = MarioKartPSP
OBJS = main.o
PSP_FW_VERSION = 660
BUILD_PRX = 0

CFLAGS = -O2 -G0 -Wall -Wextra -fno-strict-aliasing -fsingle-precision-constant
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)

LIBS = -lpspgum -lpspgu -lpspge -lpspdisplay -lpspctrl -lpspaudio -lpsppower -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Mario Kart PSP
PSP_EBOOT_SFO = PARAM.SFO

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
