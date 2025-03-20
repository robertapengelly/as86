#******************************************************************************
# @file             Makefile.wat
#******************************************************************************
SRCDIR              ?=  $(CURDIR)
VPATH               :=  $(SRCDIR)

SRC                 :=  aout.c as.c coff.c cstr.c expr.c fixup.c frag.c hashtab.c intel.c lib.c listing.c load_line.c macro.c process.c pseudo_ops.c report.c section.c symbol.c vector.c write.c write7x.c

all: as86.exe

as86.exe: $(SRC)

	wcl -fe=$@ $^

clean:

	for /r %%f in (*.obj) do ( if exist %%f ( del /q %%f ) )
	if exist as86.exe ( del /q as86.exe )
