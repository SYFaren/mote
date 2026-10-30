# include after setting ROOT
CORE = $(addprefix $(ROOT)/core/document/,buffer.c undo.c) \
	$(addprefix $(ROOT)/core/utils/,utf8.c theme.c mote_snprintf.c) \
	$(addprefix $(ROOT)/core/syntax/,hl.c regex.c) \
	$(addprefix $(ROOT)/core/editor/,editor.c) \
	$(addprefix $(ROOT)/core/system/,config.c dirlist.c keymap.c) \
	$(addprefix $(ROOT)/core/app/,app.c recovery.c)
CORE_H = $(wildcard $(ROOT)/core/*/*.h) $(ROOT)/plat/platform.h \
	$(ROOT)/plat/mote_ansi.h ../core.mk Makefile
