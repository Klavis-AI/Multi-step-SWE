--
-- PostgreSQL database dump
--

\restrict tNoWpHnWwfwO6YQUlkVixMEH0a32Cg1Z6aATStt43IHXTQ9VztPlNGz1I0U1fJu

-- Dumped from database version 10.21 (Debian 10.21-1.pgdg90+1)
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
-- Name: c10_events; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c10_events (
    id bigint NOT NULL,
    at date NOT NULL,
    kind text NOT NULL,
    payload jsonb
);


--
-- Name: c10_events_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

ALTER TABLE public.c10_events ALTER COLUMN id ADD GENERATED ALWAYS AS IDENTITY (
    SEQUENCE NAME public.c10_events_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1
);


--
-- Name: c10_part; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c10_part (
    region text NOT NULL,
    amount numeric NOT NULL
)
PARTITION BY LIST (region);


--
-- Name: c10_part_amer; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c10_part_amer (
    region text NOT NULL,
    amount numeric NOT NULL
);


--
-- Name: c10_part_emea; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c10_part_emea (
    region text NOT NULL,
    amount numeric NOT NULL
);


--
-- Name: c10_part_amer; Type: TABLE ATTACH; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c10_part ATTACH PARTITION public.c10_part_amer FOR VALUES IN ('amer');


--
-- Name: c10_part_emea; Type: TABLE ATTACH; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c10_part ATTACH PARTITION public.c10_part_emea FOR VALUES IN ('emea');


--
-- Data for Name: c10_events; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c10_events (id, at, kind, payload) FROM stdin;
1	2020-01-01	create	{"n": 1}
2	2020-01-02	update	{"n": 2}
\.


--
-- Data for Name: c10_part_amer; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c10_part_amer (region, amount) FROM stdin;
amer	20
\.


--
-- Data for Name: c10_part_emea; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c10_part_emea (region, amount) FROM stdin;
emea	10
emea	30
\.


--
-- Name: c10_events_id_seq; Type: SEQUENCE SET; Schema: public; Owner: -
--

SELECT pg_catalog.setval('public.c10_events_id_seq', 2, true);


--
-- Name: c10_events c10_events_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c10_events
    ADD CONSTRAINT c10_events_pkey PRIMARY KEY (id);


--
-- PostgreSQL database dump complete
--

\unrestrict tNoWpHnWwfwO6YQUlkVixMEH0a32Cg1Z6aATStt43IHXTQ9VztPlNGz1I0U1fJu

