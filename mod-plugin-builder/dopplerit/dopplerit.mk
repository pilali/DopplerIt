######################################
#
# dopplerit
#
# Copy this folder into mod-plugin-builder/plugins/package/ and run:
#   ./build moddwarf dopplerit      (or modduo, modduox, x86_64)
#
# To build from a local checkout instead of GitHub:
#   DOPPLERIT_SOURCE_DIR=/path/to/DopplerIt ./build moddwarf dopplerit
#
######################################

ifneq ($(DOPPLERIT_SOURCE_DIR),)
DOPPLERIT_SITE_METHOD = local
DOPPLERIT_SITE = $(DOPPLERIT_SOURCE_DIR)
DOPPLERIT_VERSION = local
else
# pin a tag or a commit hash for reproducible builds
DOPPLERIT_VERSION = claude/doppler-lv2-plugin
DOPPLERIT_SITE = https://github.com/pilali/dopplerit.git
DOPPLERIT_SITE_METHOD = git
endif

DOPPLERIT_DEPENDENCIES = lv2
DOPPLERIT_BUNDLES = dopplerit.lv2

# mod-plugin-builder provides the CPU flags of each device (NOOPT=true
# disables the native -mcpu=native detection of the Makefile)
DOPPLERIT_TARGET_MAKE = $(TARGET_MAKE_ENV) $(TARGET_CONFIGURE_OPTS) $(MAKE) NOOPT=true PREFIX=/usr -C $(@D)

define DOPPLERIT_BUILD_CMDS
	$(DOPPLERIT_TARGET_MAKE)
endef

define DOPPLERIT_INSTALL_TARGET_CMDS
	$(DOPPLERIT_TARGET_MAKE) install DESTDIR=$(TARGET_DIR)
endef

$(eval $(generic-package))
