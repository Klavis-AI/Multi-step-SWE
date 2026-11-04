--
-- PostgreSQL database dump
--

\restrict E7Mqt1OLT3H7uvP1zbuYXCTrLulEPtptcEr4YiO4hOyj4pv5AdgtC0jsMaQuFuu

-- Dumped from database version 18.6 (Debian 18.6-1.pgdg13+2)
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
-- Name: c18_temporal; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c18_temporal (
    id int4range NOT NULL,
    valid_at daterange NOT NULL
);


--
-- Name: c18_virt; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c18_virt (
    a integer NOT NULL,
    b integer GENERATED ALWAYS AS ((a + 100))
);


--
-- Data for Name: c18_temporal; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c18_temporal (id, valid_at) FROM stdin;
[1,2)	[2020-01-01,2020-06-01)
[1,2)	[2020-06-01,2021-01-01)
[2,3)	[2020-01-01,2020-02-01)
\.


--
-- Data for Name: c18_virt; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c18_virt (a) FROM stdin;
1
2
\.


--
-- Name: c18_temporal c18_pk; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c18_temporal
    ADD CONSTRAINT c18_pk PRIMARY KEY (id, valid_at WITHOUT OVERLAPS);


--
-- Name: c18_virt c18_virt_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c18_virt
    ADD CONSTRAINT c18_virt_pkey PRIMARY KEY (a);


--
-- PostgreSQL database dump complete
--

\unrestrict E7Mqt1OLT3H7uvP1zbuYXCTrLulEPtptcEr4YiO4hOyj4pv5AdgtC0jsMaQuFuu

