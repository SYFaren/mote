# Portable core shared by every overlay; include after setting ROOT.
CORE = $(addprefix $(ROOT)/core/,buffer.c utf8.c undo.c hl.c editor.c theme.c \
	config.c mote_snprintf.c regex.c dirlist.c app.c keymap.c)
CORE_H = $(wildcard $(ROOT)/core/*.h) $(ROOT)/plat/platform.h
