# Containerized external services. Included by both the agent and the verifier
# image so the inherited tests see the same services on both sides.
#
# Expects the build context to contain:
#   mirror/serve.py  mirror/mmtf-mirror-up  mirror/tls/
#   mirror/blobs/v1.0/...   mirror/svc/<host>/...
#
# JAVA_HOME must be set before this fragment runs.

COPY mirror/serve.py       /opt/mmtf-mirror/serve.py
COPY mirror/mmtf-mirror-up /usr/local/bin/mmtf-mirror-up
COPY mirror/tls/server.pem /srv/mmtf-tls/server.pem
COPY mirror/tls/ca.crt     /usr/local/share/ca-certificates/mmtf-mirror-ca.crt
COPY mirror/blobs/         /srv/mmtf/
COPY mirror/svc/           /srv/svc/

RUN set -eux; \
    chmod 0755 /usr/local/bin/mmtf-mirror-up; \
    chmod 0600 /srv/mmtf-tls/server.pem; \
    update-ca-certificates; \
    # The JVM does not read the system trust store, so the CA goes into cacerts
    # too. JDK 8 keeps it under jre/, 9+ under lib/.
    if [ -f "$JAVA_HOME/jre/lib/security/cacerts" ]; then \
        KS="$JAVA_HOME/jre/lib/security/cacerts"; \
    else \
        KS="$JAVA_HOME/lib/security/cacerts"; \
    fi; \
    keytool -importcert -noprompt -trustcacerts -alias mmtf-mirror-ca \
        -file /usr/local/share/ca-certificates/mmtf-mirror-ca.crt \
        -keystore "$KS" -storepass changeit; \
    keytool -list -keystore "$KS" -storepass changeit -alias mmtf-mirror-ca > /dev/null

# Every shell, interactive or not, brings the service up.
ENV BASH_ENV=/usr/local/bin/mmtf-mirror-up
RUN printf '. /usr/local/bin/mmtf-mirror-up\n' > /etc/profile.d/10-mmtf-mirror.sh \
    && printf '. /usr/local/bin/mmtf-mirror-up\n' >> /etc/bash.bashrc
