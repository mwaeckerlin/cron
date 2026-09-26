FROM mwaeckerlin/very-base AS cron
RUN $PKG_INSTALL g++
COPY cron.cpp .
RUN g++ -static -Os -flto=auto -fno-rtti -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,-s -std=c++20 -o cron cron.cpp
RUN strip -s -R .comment -R .gnu.version --strip-unneeded cron

# collect the whole runtime tree in /root/: the static cron binary, its
# crontab for the periodic directories, and writable temporary directories
FROM mwaeckerlin/very-base AS build
RUN mkdir -p /root/usr/bin /root/etc/cron.d /root/tmp /root/var/tmp
RUN mkdir -p /root/etc/periodic/min /root/etc/periodic/5min /root/etc/periodic/10min /root/etc/periodic/15min /root/etc/periodic/30min /root/etc/periodic/hourly /root/etc/periodic/daily /root/etc/periodic/weekly /root/etc/periodic/monthly /root/etc/periodic/yearly
RUN chmod 1777 /root/tmp /root/var/tmp
COPY --from=cron cron /root/usr/bin/cron
COPY periodic /root/etc/cron.d/periodic

#### build the final image ####
# the final image has no shell and nothing that is not required
FROM mwaeckerlin/scratch
ENV CONTAINERNAME="cron"
ENV CRON_DEBUG="0"
ENTRYPOINT ["/usr/bin/cron"]
COPY --from=build /root/ /
