# src/nls-global.mk

# Common rules for Native Language Support (NLS)
#
# If some subdirectory of the source tree wants to provide NLS, it
# needs to contain a file 'nls.mk' with the following make variable
# assignments:
#
# CATALOG_NAME          -- name of the message catalog (xxx.po); probably
#                          name of the program
# AVAIL_LANGUAGES       -- list of languages that are provided/supported
# GETTEXT_FILES         -- list of source files that contain message strings
# GETTEXT_TRIGGERS      -- (optional) list of functions that contain
#                          translatable strings
# GETTEXT_FLAGS         -- (optional) list of gettext --flag arguments to mark
#                          function arguments that contain C format strings
#                          (functions must be listed in TRIGGERS and FLAGS)
#
# That's all, the rest is done here, if --enable-nls was specified.
#
# The only user-visible targets here are 'init-po', to make an initial
# "blank" catalog from program sources, and 'update-po', which is to
# be called if the messages in the program source have changed, in
# order to merge the changes into the existing .po files.


# existence checked by Makefile.global; otherwise we won't get here
include $(srcdir)/nls.mk

# If user specified the languages he wants in --enable-nls=LANGUAGES,
# filter out the rest.  Else use all available ones.
ifdef WANTED_LANGUAGES
LANGUAGES = $(filter $(WANTED_LANGUAGES), $(AVAIL_LANGUAGES))
else
LANGUAGES = $(AVAIL_LANGUAGES)
endif

PO_FILES = $(addprefix po/, $(addsuffix .po, $(LANGUAGES)))
ALL_PO_FILES = $(addprefix po/, $(addsuffix .po, $(AVAIL_LANGUAGES)))
MO_FILES = $(addprefix po/, $(addsuffix .mo, $(LANGUAGES)))

ifdef XGETTEXT
XGETTEXT += -ctranslator --copyright-holder='PostgreSQL Global Development Group' --msgid-bugs-address=pgsql-bugs@postgresql.org --no-wrap --sort-by-file --package-name='$(CATALOG_NAME) (PostgreSQL)' --package-version='$(MAJORVERSION)' --from-code=UTF-8
endif

ifdef MSGMERGE
MSGMERGE += --no-wrap --sort-by-file
endif

# _ is defined in c.h, so it's global
GETTEXT_TRIGGERS += _
GETTEXT_FLAGS    += _:1:pass-c-format


# common settings that apply to backend and all backend modules
BACKEND_COMMON_GETTEXT_TRIGGERS = \
    errmsg errmsg_plural:1,2 \
    errdetail errdetail_log errdetail_plural:1,2 \
    errhint \
    errcontext errcause erraction errquery
BACKEND_COMMON_GETTEXT_FLAGS = \
    errmsg:1:c-format errmsg_plural:1:c-format errmsg_plural:2:c-format \
    errdetail:1:c-format errdetail_log:1:c-format errdetail_plural:1:c-format errdetail_plural:2:c-format \
    errhint:1:c-format \
    errcontext:1:c-format errcause:1:c-format erraction:1:c-format errquery:1:c-format


all-po: $(MO_FILES)

%.mo: %.po
	$(MSGFMT) -o $@ $<

ifeq ($(word 1,$(GETTEXT_FILES)),+)
po/$(CATALOG_NAME).pot: $(word 2, $(GETTEXT_FILES)) $(MAKEFILE_LIST)
ifdef XGETTEXT
	$(XGETTEXT) -D $(srcdir) -n $(addprefix -k, $(GETTEXT_TRIGGERS)) $(addprefix --flag=, $(GETTEXT_FLAGS)) -f $<
else
	@echo "You don't have 'xgettext'."; exit 1
endif
else # GETTEXT_FILES
po/$(CATALOG_NAME).pot: $(GETTEXT_FILES) $(MAKEFILE_LIST)
# Change to srcdir explicitly, don't rely on $^.  That way we get
# consistent #: file references in the po files.
ifdef XGETTEXT
	$(XGETTEXT) -D $(srcdir) -n $(addprefix -k, $(GETTEXT_TRIGGERS)) $(addprefix --flag=, $(GETTEXT_FLAGS)) $(GETTEXT_FILES)
else
	@echo "You don't have 'xgettext'."; exit 1
endif
endif # GETTEXT_FILES
	@$(MKDIR_P) $(dir $@)
	sed -e '1,18 { s/SOME DESCRIPTIVE TITLE./LANGUAGE message translation file for $(CATALOG_NAME)/;s/PACKAGE/PostgreSQL/g;s/VERSION/$(MAJORVERSION)/g;s/YEAR/'`date +%Y`'/g; }' messages.po >$@
	rm messages.po


# catalog name extensions must match behavior of PG_TEXTDOMAIN() in c.h
install-po: all-po installdirs-po
ifneq (,$(LANGUAGES))
	for lang in $(LANGUAGES); do \
	  $(INSTALL_DATA) po/$$lang.mo '$(DESTDIR)$(localedir)'/$$lang/LC_MESSAGES/$(CATALOG_NAME)$(SO_MAJOR_VERSION)-$(MAJORVERSION).mo || exit 1; \
	done
endif

installdirs-po:
	$(if $(LANGUAGES),$(MKDIR_P) $(foreach lang, $(LANGUAGES), '$(DESTDIR)$(localedir)'/$(lang)/LC_MESSAGES),:)

uninstall-po:
	$(if $(LANGUAGES),rm -f $(foreach lang, $(LANGUAGES), '$(DESTDIR)$(localedir)'/$(lang)/LC_MESSAGES/$(CATALOG_NAME)$(SO_MAJOR_VERSION)-$(MAJORVERSION).mo),:)


clean-po:
	$(if $(MO_FILES),rm -f $(MO_FILES))
	@$(if $(wildcard po/*.po.new),rm -f po/*.po.new)
	rm -f po/$(CATALOG_NAME).pot


maintainer-check-po: $(ALL_PO_FILES)
	for file in $^; do \
	  $(MSGFMT) -c -v -o /dev/null $$file || exit 1; \
	done


init-po: po/$(CATALOG_NAME).pot


# For performance reasons, only calculate these when the user actually
# requested update-po or a specific file.
ifneq (,$(filter update-po %.po.new,$(MAKECMDGOALS)))
ALL_LANGUAGES := $(shell find $(top_srcdir) -name '*.po' -print | sed 's,^.*/\([^/]*\).po$$,\1,' | LC_ALL=C sort -u)
all_compendia := $(shell find $(top_srcdir) -name '*.po' -print | LC_ALL=C sort)
else
ALL_LANGUAGES = $(AVAIL_LANGUAGES)
all_compendia = FORCE
FORCE:
endif

ifdef WANTED_LANGUAGES
ALL_LANGUAGES := $(filter $(WANTED_LANGUAGES), $(ALL_LANGUAGES))
endif

update-po: $(ALL_LANGUAGES:%=po/%.po.new)

$(AVAIL_LANGUAGES:%=po/%.po.new): po/%.po.new: po/%.po po/$(CATALOG_NAME).pot $(all_compendia)
	$(MSGMERGE) --lang=$* $(word 1, $^) $(word 2,$^) -o $@ $(addprefix --compendium=,$(filter %/$*.po,$(wordlist 3,$(words $^),$^)))

# For languages not yet available, merge against oneself, to pick
# up translations from the compendia.  (Merging against /dev/null
# doesn't work so well; it inserts the headers from the first-named
# compendium.)
po/%.po.new: po/$(CATALOG_NAME).pot $(all_compendia)
	$(MSGMERGE) --lang=$* $(word 1,$^) $(word 1,$^) -o $@ $(addprefix --compendium=,$(filter %/$*.po,$(wordlist 2,$(words $^),$^)))


all: all-po
install: install-po
installdirs: installdirs-po
uninstall: uninstall-po
clean distclean maintainer-clean: clean-po
maintainer-check: maintainer-check-po

.PHONY: all-po install-po installdirs-po uninstall-po clean-po \
        maintainer-check-po init-po update-po
