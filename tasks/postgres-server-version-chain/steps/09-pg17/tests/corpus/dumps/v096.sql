--
-- PostgreSQL database dump
--

\restrict iiyqa6xuPsYyGV0mXYgoIQ42QQy8Cp8XuySJdklUIDpCh5P1gldfo4891aJj0A2

-- Dumped from database version 9.6.24
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
-- Name: c96_items; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c96_items (
    order_id integer,
    sku text,
    qty integer DEFAULT 1 NOT NULL,
    price numeric(8,2)
);


--
-- Name: c96_orders; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.c96_orders (
    id integer NOT NULL,
    customer text NOT NULL,
    total numeric(10,2) NOT NULL,
    placed date NOT NULL,
    tags text[],
    doc jsonb,
    CONSTRAINT c96_orders_total_check CHECK ((total >= (0)::numeric))
);


--
-- Name: c96_orders_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.c96_orders_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: c96_orders_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.c96_orders_id_seq OWNED BY public.c96_orders.id;


--
-- Name: c96_totals; Type: VIEW; Schema: public; Owner: -
--

CREATE VIEW public.c96_totals AS
 SELECT o.id,
    o.customer,
    sum(((i.qty)::numeric * i.price)) AS amount
   FROM (public.c96_orders o
     JOIN public.c96_items i ON ((i.order_id = o.id)))
  GROUP BY o.id, o.customer;


--
-- Name: c96_orders id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c96_orders ALTER COLUMN id SET DEFAULT nextval('public.c96_orders_id_seq'::regclass);


--
-- Data for Name: c96_items; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c96_items (order_id, sku, qty, price) FROM stdin;
1	x1	2	5.25
1	x2	1	0.00
2	y1	3	33.33
\.


--
-- Data for Name: c96_orders; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.c96_orders (id, customer, total, placed, tags, doc) FROM stdin;
1	alice	10.50	2019-01-02	{a,b}	{"k": 1}
2	bob	99.99	2019-06-30	{}	{"k": [1, 2], "z": null}
3	carol	0.00	2020-02-29	\N	\N
\.


--
-- Name: c96_orders_id_seq; Type: SEQUENCE SET; Schema: public; Owner: -
--

SELECT pg_catalog.setval('public.c96_orders_id_seq', 3, true);


--
-- Name: c96_orders c96_orders_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c96_orders
    ADD CONSTRAINT c96_orders_pkey PRIMARY KEY (id);


--
-- Name: c96_items_sku; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX c96_items_sku ON public.c96_items USING btree (sku);


--
-- Name: c96_items c96_items_order_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.c96_items
    ADD CONSTRAINT c96_items_order_id_fkey FOREIGN KEY (order_id) REFERENCES public.c96_orders(id) ON DELETE CASCADE;


--
-- PostgreSQL database dump complete
--

\unrestrict iiyqa6xuPsYyGV0mXYgoIQ42QQy8Cp8XuySJdklUIDpCh5P1gldfo4891aJj0A2

