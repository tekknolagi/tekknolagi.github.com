# Set things up
FROM alpine:latest as build
ARG VER=3.0.2
ARG COSMO=cosmos-$VER.zip
RUN wget https://github.com/jart/cosmopolitan/releases/download/$VER/$COSMO
WORKDIR cosmo
COPY _site/. _site
RUN unzip ../$COSMO bin/ape.elf bin/assimilate bin/zip bin/redbean
WORKDIR _site
RUN sh ../bin/zip -A -r ../bin/redbean *
WORKDIR ..
RUN bin/ape.elf bin/assimilate bin/redbean

# Set up the container
FROM scratch
COPY --from=build /cosmo/bin/redbean .
EXPOSE 8000
ENTRYPOINT ["./redbean"]
