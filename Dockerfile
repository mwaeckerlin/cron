FROM mwaeckerlin/very-base as build
RUN $PKG_INSTALL dcron libcap
COPY crontabs /etc/crontabs/$RUN_USER
RUN chmod go= /etc/crontabs/$RUN_USER
RUN rm /etc/crontabs/root
RUN mkdir /etc/periodic/min /etc/periodic/5min /etc/periodic/10min /etc/periodic/30min /etc/periodic/yearly
RUN $ALLOW_USER /usr/sbin/crond /var/tmp
RUN setcap cap_setgid=ep /usr/sbin/crond

FROM mwaeckerlin/scratch as production
ENV CONTAINERNAME "cron"
COPY --from=build /lib/ld-musl-x86_64.so.* /lib/
COPY --from=build /usr/lib/libcap.so* /usr/lib/
COPY --from=build /bin /bin
COPY --from=build --chown=$RUN_USER /tmp /tmp
COPY --from=build /usr/sbin /usr/sbin
COPY --from=build --chown=$RUN_USER /etc/crontabs/$RUN_USER /etc/crontabs/$RUN_USER
COPY --from=build /etc/periodic /etc/periodic
COPY --from=build /var/spool/cron /var/spool/cron
COPY logger /bin/logger
COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]