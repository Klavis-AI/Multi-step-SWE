--
-- PostgreSQL database dump
--

\restrict TpOOqFrDZpcsiFlJpaRs4cFLePLH6eCA1dBZ3Nv0bNuPv6fPbc3VQtJ33Ymnj7l

-- Dumped from database version 16.15 (Debian 16.15-1.pgdg13+2)
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
-- Name: c16_any; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c16_any (
    g integer,
    v integer
);


--
-- Name: c16_json; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c16_json (
    id integer NOT NULL,
    doc jsonb,
    arr jsonb
);


--
-- Data for Name: c16_any; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c16_any (g, v) FROM stdin;
1	5
1	5
2	9
\.


--
-- Data for Name: c16_json; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c16_json (id, doc, arr) FROM stdin;
1	{"k": 1, "l": "v"}	[1, "a"]
2	{"x": true}	[]
\.


--
-- Name: c16_json c16_json_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c16_json
    ADD CONSTRAINT c16_json_pkey PRIMARY KEY (id);


--
-- PostgreSQL database dump complete
--

\unrestrict TpOOqFrDZpcsiFlJpaRs4cFLePLH6eCA1dBZ3Nv0bNuPv6fPbc3VQtJ33Ymnj7l

