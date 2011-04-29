# This version uses MainConcept whose 8.5 UCC_Transform produces a SIGFPE
# for YUYV to RGB conversion (sigh).

XCPPFLAGS=-pthread\
 -DUSE_MC=1\
 -I$(topdir)/platforms/Cross/plugins/QwaqLib\
 -I$(topdir)/platforms/Cross/third-party/boost_1_40_0\
 -I../../third-party/MainConcept/AVCSDK-LX32_8.6.0_LINUX/include\
 -I../../third-party/MainConcept/CODECSDK-LX32_8.6.0_2010-02-22/include

XCFLAGS=-DUSE_MC=1

# -Bsymbolic to get self-contained shared objects.  This should be in libtool
# construction but we don't control e.g. /usr/share/libtool/ltmain.sh. Sigh
PLIBS=-Wl,-Bsymbolic\
 -lpthread\
 -L../../third-party/MainConcept/CODECSDK-LX32_8.6.0_2010-02-22/bin\
  -ldemo_trans_video_colorspace\
 -L../../libboost\
  -lboost_thread