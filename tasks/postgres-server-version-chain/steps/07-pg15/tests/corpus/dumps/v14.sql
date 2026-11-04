--
-- PostgreSQL database dump
--

\restrict 3vWTk7ev3jaJKZgIf4lKtdjJAoOvk1pp6ZxhqB4ZhFLVVa0zF6TX35tSeuLjQrq

-- Dumped from database version 14.24 (Debian 14.24-1.pgdg13+2)
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
-- Name: c14_ex; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c14_ex (
    ts timestamp without time zone,
    n numeric
);


--
-- Name: c14_mr; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c14_mr (
    id integer NOT NULL,
    spans int4multirange,
    j jsonb
);


--
-- Data for Name: c14_ex; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c14_ex (ts, n) FROM stdin;
2020-01-02 03:04:05.5	1.5
\.


--
-- Data for Name: c14_mr; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c14_mr (id, spans, j) FROM stdin;
1	{[1,3),[5,7)}	{"a": {"b": 1}}
2	{}	{"a": [1, 2]}
3	{[10,20)}	{}
\.


--
-- Name: c14_mr c14_mr_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c14_mr
    ADD CONSTRAINT c14_mr_pkey PRIMARY KEY (id);


--
-- PostgreSQL database dump complete
--

\unrestrict 3vWTk7ev3jaJKZgIf4lKtdjJAoOvk1pp6ZxhqB4ZhFLVVa0zF6TX35tSeuLjQrq

