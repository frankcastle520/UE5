FROM node:20.11.0 as build-env
ARG VersionInfo
ENV VITE_VERSION_INFO=${VersionInfo}
WORKDIR /app

COPY Programs/Horde/HordeDashboard ./Dashboard
RUN cd ./Dashboard && npm install --legacy-peer-deps
RUN cd ./Dashboard && npm run build

