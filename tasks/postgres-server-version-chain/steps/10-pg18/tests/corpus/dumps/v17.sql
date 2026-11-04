--
-- PostgreSQL database dump
--

\restrict kZgU0Ay1Dk1SzU9zhDeIb04q7ph3uctWSFm9jsVL6kzdHtVykEHjTxj9x23BV6G

-- Dumped from database version 17.11 (Debian 17.11-1.pgdg13+2)
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

SET default_table_access_method = heap;

--
-- Name: c17_jt; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c17_jt (
    id integer NOT NULL,
    doc jsonb
);


--
-- Name: c17_m; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c17_m (
    id integer NOT NULL,
    v integer
);


--
-- Data for Name: c17_jt; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c17_jt (id, doc) FROM stdin;
1	[{"a": 1, "b": "x"}, {"a": 2, "b": "y"}]
2	[]
\.


--
-- Data for Name: c17_m; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c17_m (id, v) FROM stdin;
1	1
2	2
3	3
\.


--
-- Name: c17_jt c17_jt_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c17_jt
    ADD CONSTRAINT c17_jt_pkey PRIMARY KEY (id);


--
-- Name: c17_m c17_m_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c17_m
    ADD CONSTRAINT c17_m_pkey PRIMARY KEY (id);


--
-- PostgreSQL database dump complete
--

\unrestrict kZgU0Ay1Dk1SzU9zhDeIb04q7ph3uctWSFm9jsVL6kzdHtVykEHjTxj9x23BV6G

