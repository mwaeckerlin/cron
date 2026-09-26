# The image under test, derived from mwaeckerlin/cron exactly the way the
# README describes it: the programs the jobs call are installed in a build
# stage and copied with their libraries onto the cron image.
FROM mwaeckerlin/very-base AS build
RUN $PKG_INSTALL coreutils
RUN tar cph $(which echo printenv id false whoami sleep touch) \
    $(for f in $(which echo printenv id false whoami sleep touch); do \
    ldd $f | sed -n 's,.* => \([^ ]*\) .*,\1,p'; \
    done) \
    | tar xpC /root/
RUN mkdir -p /root/etc/periodic/min
RUN cp -L $(which whoami) /root/etc/periodic/min/whoami

FROM mwaeckerlin/cron
COPY --from=build /root/ /
COPY cron.d/ /etc/cron.d/
COPY check/ /etc/cron-e2e/
