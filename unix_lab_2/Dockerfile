FROM alpine:3.22

RUN apk add --no-cache flock && mkdir -p /data
COPY file_creator.sh /usr/local/bin/file_creator.sh

ENTRYPOINT ["/bin/sh", "/usr/local/bin/file_creator.sh"]
