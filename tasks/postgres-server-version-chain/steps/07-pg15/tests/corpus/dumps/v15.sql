--
-- PostgreSQL database dump
--

\restrict kYRLYj362Purx3gz2hSZoZ9f3PfQWxfWFPDn6ubbfV4d4uPTFsd7oy9ukLIG8fH

-- Dumped from database version 15.19 (Debian 15.19-1.pgdg13+2)
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
-- Name: c15_source; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c15_source (
    id integer,
    v integer
);


--
-- Name: c15_target; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c15_target (
    id integer NOT NULL,
    v integer
);


--
-- Name: c15_sv; Type: VIEW; Schema: public; Owner: -
--

CREATE VIEW public.c15_sv WITH (security_invoker='true') AS
 SELECT c15_target.id,
    c15_target.v
   FROM public.c15_target;


--
-- Data for Name: c15_source; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c15_source (id, v) FROM stdin;
2	20
3	30
\.


--
-- Data for Name: c15_target; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c15_target (id, v) FROM stdin;
1	1
2	20
3	30
\.


--
-- Name: c15_target c15_target_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c15_target
    ADD CONSTRAINT c15_target_pkey PRIMARY KEY (id);


--
-- PostgreSQL database dump complete
--

\unrestrict kYRLYj362Purx3gz2hSZoZ9f3PfQWxfWFPDn6ubbfV4d4uPTFsd7oy9ukLIG8fH

