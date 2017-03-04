CHAPTERS = 00_fundamentals.md 01_booleans.md 02_symbols.md 03_lists.md \
		   04_environments.md 05_if.md 06_prim1.md 07_prim2.md 08_asts.md \
		   09_quote.md 10_closures.md 11_prim3.md 12_metacircular.md

all: $(CHAPTERS:.md=.pdf)

%.pdf: %.md
	pandoc --latex-engine=pdflatex $< -o $@

clean:
	rm *.aux *.out *.pdf

# %.tex: %.md
# 	pandoc -f markdown -t xetex -s $< > $@
