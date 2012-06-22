INSERT INTO version (table_name, table_version) values ('silo','7');
CREATE TABLE silo (
    id SERIAL PRIMARY KEY NOT NULL,
    src_addr VARCHAR(128) DEFAULT '' NOT NULL,
    dst_addr VARCHAR(128) DEFAULT '' NOT NULL,
    username VARCHAR(64) DEFAULT '' NOT NULL,
    domain VARCHAR(64) DEFAULT '' NOT NULL,
    inc_time INTEGER DEFAULT 0 NOT NULL,
    exp_time INTEGER DEFAULT 0 NOT NULL,
    snd_time INTEGER DEFAULT 0 NOT NULL,
    ctype VARCHAR(32) DEFAULT 'text/plain' NOT NULL,
    body BYTEA DEFAULT '' NOT NULL,
    extra_hdrs TEXT DEFAULT '' NOT NULL,
    callid VARCHAR(128) DEFAULT '' NOT NULL,
    status INTEGER DEFAULT 0 NOT NULL
);

CREATE INDEX silo_account_idx ON silo (username, domain);

