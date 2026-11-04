--
-- PostgreSQL database dump
--

\restrict f9TzKmkuV4GdAAoGGpUWSA0RcuDHsk7XDHtyrsdeFs4G28iVjaSI8KvogbSfXqC

-- Dumped from database version 11.16 (Debian 11.16-1.pgdg90+1)
-- Dumped by pg_dump version 18.6 (Debian 18.6-1.pgdg12+2)

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
-- (authoring normalisation) SET transaction_timeout removed: pg_dump 18 emits it for every source version
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: public; Type: SCHEMA; Schema: -; Owner: -
--

-- (authoring normalisation) CREATE SCHEMA public; removed: the target database already has it


SET default_tablespace = '';

--
-- Name: c11_cov; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c11_cov (
    a integer,
    b integer,
    c text
);


--
-- Name: c11_hash; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c11_hash (
    id integer NOT NULL,
    v text
)
PARTITION BY HASH (id);


--
-- Name: c11_hash_0; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c11_hash_0 (
    id integer NOT NULL,
    v text
);


--
-- Name: c11_hash_1; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c11_hash_1 (
    id integer NOT NULL,
    v text
);


--
-- Name: c11_hash_0; Type: TABLE ATTACH; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c11_hash ATTACH PARTITION public.c11_hash_0 FOR VALUES WITH (modulus 2, remainder 0);


--
-- Name: c11_hash_1; Type: TABLE ATTACH; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c11_hash ATTACH PARTITION public.c11_hash_1 FOR VALUES WITH (modulus 2, remainder 1);


--
-- Data for Name: c11_cov; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c11_cov (a, b, c) FROM stdin;
1	10	x
2	20	y
\.


--
-- Data for Name: c11_hash_0; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c11_hash_0 (id, v) FROM stdin;
1	v1
2	v2
\.


--
-- Data for Name: c11_hash_1; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c11_hash_1 (id, v) FROM stdin;
3	v3
4	v4
5	v5
6	v6
7	v7
8	v8
\.


--
-- Name: c11_cov_ix; Type: INDEX; Schema: public; Owner: -
--

CREATE UNIQUE INDEX c11_cov_ix ON public.c11_cov USING btree (a) INCLUDE (b);


--
-- PostgreSQL database dump complete
--

\unrestrict f9TzKmkuV4GdAAoGGpUWSA0RcuDHsk7XDHtyrsdeFs4G28iVjaSI8KvogbSfXqC

