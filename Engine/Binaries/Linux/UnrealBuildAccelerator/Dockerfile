FROM ubuntu:24.04
WORKDIR /app
COPY * /app/
RUN chmod +x ./UbaAgent ./UbaCacheService ./UbaCli ./UbaStorageProxy
ENTRYPOINT ["/app/UbaCli"]