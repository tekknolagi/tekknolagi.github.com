FROM ruby:3.4-alpine as build_env
ENV BUNDLE_FORCE_RUBY_PLATFORM true
RUN apk add --no-cache build-base python3 linux-headers
RUN mkdir /site
COPY Gemfile /site
COPY Gemfile.lock /site
COPY .ruby-version /site
WORKDIR /site
RUN bundle

FROM alpine:latest as redbean
ARG VER=3.0.2
ARG COSMO=cosmos-$VER.zip
RUN wget https://github.com/jart/cosmopolitan/releases/download/$VER/$COSMO
WORKDIR cosmo
RUN unzip ../$COSMO bin/ape.elf bin/assimilate bin/zip bin/redbean
RUN bin/ape.elf bin/assimilate bin/redbean

FROM build_env as build_site
COPY . /site
RUN rm /site/Gemfile.lock
RUN bundle exec jekyll build --future

FROM redbean as build_server
COPY --from=build_site /site/_site/. _site
WORKDIR _site
RUN sh ../bin/zip -A -r ../bin/redbean *

FROM busybox as web
COPY --from=build_site /site/_site/. .
CMD ["busybox", "httpd", "-f", "-v", "-p", "8000"]
EXPOSE 8000
