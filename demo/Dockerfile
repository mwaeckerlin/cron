# install the programs the jobs call, and collect them with their
# libraries in /root/ (tar h stores a symbolic link as the file it names)
FROM mwaeckerlin/very-base AS build
RUN $PKG_INSTALL coreutils
RUN tar cph $(which date echo) \
    $(for f in $(which date echo); do \
    ldd $f | sed -n 's,.* => \([^ ]*\) .*,\1,p'; \
    done) \
    | tar xpC /root/

# the cron image with the programs and the schedule
FROM mwaeckerlin/cron
ENV CONTAINERNAME="cron-demo"
COPY --from=build /root/ /
COPY --from=build /root/bin/date /etc/periodic/min/date
COPY demo /etc/cron.d/demo
