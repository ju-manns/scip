FLAGS		+=	-DNDEBUG -DSCIP_ROUNDING_FE
OFLAGS		+=	-g -O0 -fomit-frame-pointer
CFLAGS		+=	$(GCCWARN) -Wno-strict-aliasing -Wno-missing-declarations -Wno-missing-prototypes
CXXFLAGS	+=	$(GXXWARN) -Wno-strict-aliasing # -fno-exceptions (CLP uses exceptions)
