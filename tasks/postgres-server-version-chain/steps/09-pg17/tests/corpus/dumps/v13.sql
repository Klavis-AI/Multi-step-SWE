--
-- PostgreSQL database dump
--

\restrict UoV1XZ32wHReut2Jql1XLXIcQJdbNDgjF7lxVaY9D33fqBjUmrXt5uiOfVrr1qJ

-- Dumped from database version 13.23 (Debian 13.23-1.pgdg13+1)
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
-- Name: c13_part; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c13_part (
    i integer NOT NULL,
    t text
)
PARTITION BY RANGE (i);


SET default_table_access_method = heap;

--
-- Name: c13_part_def; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c13_part_def (
    i integer NOT NULL,
    t text
);


--
-- Name: c13_part_hi; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c13_part_hi (
    i integer NOT NULL,
    t text
);


--
-- Name: c13_part_lo; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c13_part_lo (
    i integer NOT NULL,
    t text
);


--
-- Name: c13_ties; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c13_ties (
    g integer,
    v integer
);


--
-- Name: c13_part_def; Type: TABLE ATTACH; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c13_part ATTACH PARTITION public.c13_part_def DEFAULT;


--
-- Name: c13_part_hi; Type: TABLE ATTACH; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c13_part ATTACH PARTITION public.c13_part_hi FOR VALUES FROM (100) TO (1000);


--
-- Name: c13_part_lo; Type: TABLE ATTACH; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c13_part ATTACH PARTITION public.c13_part_lo FOR VALUES FROM (0) TO (100);


--
-- Data for Name: c13_part_def; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c13_part_def (i, t) FROM stdin;
9999	c
\.


--
-- Data for Name: c13_part_hi; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c13_part_hi (i, t) FROM stdin;
150	b
\.


--
-- Data for Name: c13_part_lo; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c13_part_lo (i, t) FROM stdin;
1	a
\.


--
-- Data for Name: c13_ties; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c13_ties (g, v) FROM stdin;
1	10
1	20
1	20
2	5
2	7
2	7
\.


--
-- PostgreSQL database dump complete
--

\unrestrict UoV1XZ32wHReut2Jql1XLXIcQJdbNDgjF7lxVaY9D33fqBjUmrXt5uiOfVrr1qJ

