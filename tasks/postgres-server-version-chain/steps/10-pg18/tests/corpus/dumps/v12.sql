--
-- PostgreSQL database dump
--

\restrict j94LAjRnbYuuWcsMil5LeR6phhdxPiIse1Aytubs9Bie0ZPFEK77v5gCNRwdeVT

-- Dumped from database version 12.22 (Debian 12.22-1.pgdg120+1)
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
-- Name: c12_float; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c12_float (
    f4 real,
    f8 double precision,
    n numeric
);


--
-- Name: c12_gen; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c12_gen (
    a integer NOT NULL,
    b integer GENERATED ALWAYS AS ((a * 2)) STORED,
    j jsonb
);


--
-- Data for Name: c12_float; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c12_float (f4, f8, n) FROM stdin;
0.1	0.3333333333333333	0.33333333333333333333
\.


--
-- Data for Name: c12_gen; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c12_gen (a, j) FROM stdin;
1	{"p": [1, 2, 3]}
2	{"p": []}
3	\N
\.


--
-- Name: c12_gen c12_gen_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c12_gen
    ADD CONSTRAINT c12_gen_pkey PRIMARY KEY (a);


--
-- PostgreSQL database dump complete
--

\unrestrict j94LAjRnbYuuWcsMil5LeR6phhdxPiIse1Aytubs9Bie0ZPFEK77v5gCNRwdeVT

