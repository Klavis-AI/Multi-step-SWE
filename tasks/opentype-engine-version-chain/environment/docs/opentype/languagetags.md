
# Language system tags (OpenType 1.9.1) - Typography | Microsoft Learn

Language system tags identify the language systems supported in OpenType Layout tables. What is meant by a “language system” in this context is a set of typographic conventions for how text in a given script should be presented. Such conventions may be associated with particular languages, with particular genres of usage, with different publications, and other such factors. For example, particular glyph variants for certain characters may be required for particular languages, or for phonetic transcription or mathematical notation.

In principle, a given set of conventions may be shared across multiple scenarios. For instance, two different languages (perhaps unrelated) may happen to follow the same conventions. Language system tags can be registered on a perceived-need basis, however. As a result, there is no guarantee that each tag represents a distinct and unique set of conventions. Tags can, however, be registered with the intent of representing conventions that apply to multiple languages. In such cases, the documented description for the tag should reflect that intent.

It should also be noted that there may be more than one set of typographic conventions that apply to a given language.

Therefore, in several respects, language system tags do not correspond in a one-to-one manner with languages. Even so, many registered tags are intended to represent typographic conventions for a particular language. For cases in which a correlation exists between a tag and one or more languages, the language identities are documented here by reference to the ISO 639 standard for language identifiers.

> 
> *Note:* The ISO 639 standard defines different identifier sets. Set 3 includes identifiers for all known languages; Set 2 also includes identifiers for certain collections of languages. Entities from ISO 639 that are referenced here can include any of the individual languages covered in Set 3, or any of the collections covered in Set 2.

> 
> *Note:* The ISO 639 data for a given language system can include identifiers that are equivalent or narrower in scope. For example, when a language system corresponds to an ISO 639 collection, the ISO 639 identifiers for individual languages within that collection may also be listed. On the other hand, if a language system corresponds to an ISO 639 individual language identifier, ISO 639 collection identifiers are not also listed. In some cases, an individual language identifier may be listed for multiple language system tags.

If information is available to an application declaring the language of text content, then the application may make use of that to select a default language system tag to be applied when displaying that text. In some applications, however, it could be preferable to give users control over the choice of language system tag to be used. Depending on the application scenario, such control may be given to content authors, to content readers, or to both.

All tags are four-character strings composed of a limited set of ASCII characters; for details regarding the Tag data type, see [Data Types](otff#data-types). By convention, registered language system tags use three or four capital letters (0x41 to 0x5A).

> 
> *Note:* The tags 'dflt' and 'DFLT', as language system tags, are permanently reserved and are not used in OpenType fonts. Within the structure of GPOS and GSUB tables, data is organized by [Script tables](chapter2#script-table), and each Script table has an offset to a default [language system (LangSys) table](chapter2#language-system-table). These structures do not involve use of any language system tag. Some font development tools may use 'dflt' or 'DFLT' as a means to indicate data that should get compiled into a default LangSys table. That is a tool-specific convention, and it is the responsibility of font tool developers to compile data into appropriate structures with the font file. An OpenType font should never include language system records (LangSysRecord) with the 'dflt' or 'DFLT' tag.

Requests to register additional language system tags can be submitted using the feedback mechanism at the bottom of this page or at https://github.com/MicrosoftDocs/typography-issues/issues.

In the following table, entries marked with an asterisk (“\*”) have been added to the registry since the current OpenType version was published.

| Language system | Language system tag | Corresponding ISO 639 IDs or other information |
| --- | --- | --- |
| Eastern Abenaki | 'AAQ ' | aaq |
| Abaza | 'ABA ' | abq |
| Abkhazian | 'ABK ' | abk |
| Acholi | 'ACH ' | ach |
| Achi | 'ACR ' | acr |
| Cypriot Arabic | 'ACY ' | acy |
| Adyghe | 'ADY ' | ady |
| Afrikaans | 'AFK ' | afr |
| Afar | 'AFR ' | aar |
| Agaw | 'AGW ' | ahg |
| Aiton | 'AIO ' | aio |
| Akan | 'AKA ' | aka, fat, twi |
| Batak Angkola | 'AKB ' | akb |
| Alsatian | 'ALS ' | gsw |
| Altai | 'ALT ' | atv, alt |
| Amharic | 'AMH ' | amh |
| Anglo-Saxon | 'ANG ' | ang |
| Phonetic transcription—Americanist conventions | APPH | Cf. BCP 47 variant subtag “fonnapa” (see https://www.iana.org/assignments/lang-subtags-templates/fonnapa.txt) |
| Arabic | 'ARA ' | ara |
| Aragonese | 'ARG ' | arg |
| Aari | 'ARI ' | aiw |
| Rakhine | 'ARK ' | mhv, rmz, rki |
| Assamese | 'ASM ' | asm |
| Asturian | 'AST ' | ast |
| Athapaskan languages | 'ATH ' | aht, apa, apk, apj, apl, apm, apw, ath, bea, sek, bcr, caf, chp, clc, coq, crx, ctc, den, dgr, gce, gwi, haa, hoi, hup, ing, kkz, koy, ktw, kuu, mvb, nav, qwt, scs, srs, taa, tau, tcb, tce, tfn, tgx, tht, tol, ttm, tuu, txc, wlk, xup, xsl |
| Gros Ventre (Atsina) | 'ATS ' | ats |
| Avatime | 'AVN ' | avn |
| Avar | 'AVR ' | ava |
| Awadhi | 'AWA ' | awa |
| Aymara | 'AYM ' | aym |
| Torki | 'AZB ' | azb |
| Azerbaijani | 'AZE ' | aze |
| Badaga | 'BAD ' | bfq |
| Banda | BAD0 | bad, bbp, bfl, bjo, bpd, bqk, gox, kuw, liy, lna, lnl, mnh, nue, nuu, tor, yaj, zmz |
| Baghelkhandi | 'BAG ' | bfy |
| Balkar | 'BAL ' | krc |
| Balinese | 'BAN ' | ban |
| Bavarian | 'BAR ' | bar |
| Baulé | 'BAU ' | bci |
| Batak Toba | 'BBC ' | bbc |
| Berber | 'BBR ' | auj, ber, cnu, gha, gho, grr, jbe, jbn, kab, mzb, oua, rif, sds, shi, shy, siz, sjs, swn, taq, tez, thv, thz, tia, tjo, tmh, ttq, tzm, zen, zgh |
| Bench | 'BCH ' | bcq |
| Bible Cree | 'BCR ' |  |
| Emberá-Baudó | 'BDC ' | bdc |
| Bandjalang | 'BDY ' | bdy |
| Belarusian | 'BEL ' | bel |
| Bemba | 'BEM ' | bem |
| Bangla | 'BEN ' | ben |
| Haryanvi | 'BGC ' | bgc |
| Bagri | 'BGQ ' | bgq |
| Bulgarian | 'BGR ' | bul |
| Bhili | 'BHI ' | bhi, bhb |
| Bhojpuri | 'BHO ' | bho |
| Bikol | 'BIK ' | bik, bhk, bcl, bto, cts, bln, fbl, lbl, rbl, ubl |
| Bilen | 'BIL ' | byn |
| Bislama | 'BIS ' | bis |
| Kanauji | 'BJJ ' | bjj |
| Blackfoot | 'BKF ' | bla |
| Baluchi | 'BLI ' | bal |
| Pa’o Karen | 'BLK ' | blk |
| Balante | 'BLN ' | bjt, ble |
| Balti | 'BLT ' | bft |
| Bambara (Bamanankan) | 'BMB ' | bam |
| Bamileke | 'BML ' | bai, bbj, bko, byv, fmp, jgo, nla, nnh, nnz, nwe, xmg, ybb |
| Bosnian | 'BOS ' | bos |
| Bishnupriya Manipuri | 'BPY ' | bpy |
| Breton | 'BRE ' | bre |
| Brahui | 'BRH ' | brh |
| Braj Bhasha | 'BRI ' | bra |
| Burmese | 'BRM ' | mya |
| Bodo | 'BRX ' | brx |
| Bashkir | 'BSH ' | bak |
| Burushaski | 'BSK ' | bsk |
| Batak Dairi (Pakpak) | 'BTD ' | btd |
| Beti | 'BTI ' | btb, beb, bum, bxp, eto, ewo, mct |
| Batak languages | 'BTK ' | akb, bbc, btd, btk, btm, bts, btx, btz |
| Batak Mandailing | 'BTM ' | btm |
| Batak Simalungun | 'BTS ' | bts |
| Batak Karo | 'BTX ' | btx |
| Batak Alas-Kluet | 'BTZ ' | btz |
| Bugis | 'BUG ' | bug |
| Medumba | 'BYV ' | byv |
| Kaqchikel | 'CAK ' | cak |
| Catalan | 'CAT ' | cat |
| Cayuga | 'CAY ' | cay |
| Chimila | 'CBG ' | cbg |
| Zamboanga Chavacano | 'CBK ' | cbk |
| Chinantec | CCHN | cco, chj, chq, chz, cle, cnl, cnt, cpa, csa, cso, cte, ctl, cuc, cvn |
| Cebuano | 'CEB ' | ceb |
| Chiga | 'CGG ' | cgg |
| Chamorro | 'CHA ' | cha |
| Chechen | 'CHE ' | che |
| Chaha Gurage | 'CHG ' | sgw |
| Chattisgarhi | 'CHH ' | hne |
| Chichewa (Chewa, Nyanja) | 'CHI ' | nya |
| Chukchi | 'CHK ' | ckt |
| Chuukese | CHK0 | chk |
| Choctaw | 'CHO ' | cho |
| Chipewyan | 'CHP ' | chp |
| Cherokee | 'CHR ' | chr |
| Chuvash | 'CHU ' | chv |
| Cheyenne | 'CHY ' | chy |
| Western Cham | 'CJA ' | cja |
| Eastern Cham | 'CJM ' | cjm |
| Emberá-Chamí | 'CMI ' | cmi |
| Comorian | 'CMR ' | swb, wlc, wni, zdj |
| Comox | 'COO ' | coo |
| Coptic | 'COP ' | cop |
| Cornish | 'COR ' | cor |
| Corsican | 'COS ' | cos |
| Creoles | 'CPP ' | abs, acf, afs, aig, aoa, bah, bew, bis, bjs, bpl, bpq, brc, bxo, bzj, bzk, cbk, ccl, ccm, chn, cks, cpe, cpf, cpi, cpp, cri, crp, crs, dcr, dep, djk, fab, fng, fpe, gac, gcf, gcl, gcr, gib, goq, gpe, gul, gyn, hat, hca, hmo, hwc, icr, idb, ihb, jam, jvd, kcn, kea, kmv, kri, kww, lir, lou, lrt, max, mbf, mcm, mfe, mfp, mkn, mod, msi, mud, mzs, nag, nef, ngm, njt, onx, oor, pap, pcm, pea, pey, pga, pih, pis, pln, pml, pmy, pov, pre, rcf, rop, scf, sci, skw, srm, srn, sta, svc, tas, tch, tcs, tgh, tmg, tpi, trf, tvy, uln, vic, vkp, wes, xmm, ycr |
| Cree | 'CRE ' | cre |
| Carrier | 'CRR ' | crx, caf |
| Crimean Tatar | 'CRT ' | crh |
| Kashubian | 'CSB ' | csb |
| Church Slavonic | 'CSL ' | chu |
| Czech | 'CSY ' | ces |
| Chittagonian | 'CTG ' | ctg |
| Emberá-Catío | 'CTO ' | cto |
| Wayanad Chetti | 'CTT ' | ctt |
| San Blas Kuna | 'CUK ' | cuk |
| Dagbani | 'DAG ' | dag |
| Danish | 'DAN ' | dan |
| Dargwa | 'DAR ' | dar |
| Dayi | 'DAX ' | dax |
| Woods Cree | 'DCR ' | cwd |
| German | 'DEU ' | deu |
| Dogri (individual language) | 'DGO ' | dgo |
| Dogri (macrolanguage) | 'DGR ' | doi |
| Dhangu | 'DHG ' | dhg |
| Divehi (Dhivehi, Maldivian) | 'DHV ' (deprecated) | div |
| Dimli | 'DIQ ' | diq |
| Divehi (Dhivehi, Maldivian) | 'DIV ' | div |
| Zarma | 'DJR ' | dje |
| Djambarrpuyngu | DJR0 | djr |
| Dangme | 'DNG ' | ada |
| Dan | 'DNJ ' | dnj |
| Dinka | 'DNK ' | din |
| Dari | 'DRI ' | prs |
| Dhuwal | 'DUJ ' | duj, dwu, dwy |
| Dungan | 'DUN ' | dng |
| Dzongkha | 'DZN ' | dzo |
| Ebira | 'EBI ' | igb |
| Eastern Cree | 'ECR ' | crj, crl |
| Edo | 'EDO ' | bin |
| Efik | 'EFI ' | efi |
| Greek | 'ELL ' | ell |
| Eastern Maninkakan | 'EMK ' | emk |
| Northern Emberá | 'EMP ' | emp |
| English | 'ENG ' | eng |
| Erzya | 'ERZ ' | myv |
| Spanish | 'ESP ' | spa |
| Central Yupik | 'ESU ' | esu |
| Estonian | 'ETI ' | est |
| Basque | 'EUQ ' | eus |
| Evenki | 'EVK ' | evn |
| Even | 'EVN ' | eve |
| Ewe | 'EWE ' | ewe |
| French Antillean | 'FAN ' | acf |
| Fang | FAN0 | fan |
| Persian | 'FAR ' | fas |
| Fanti | 'FAT ' | fat |
| Finnish | 'FIN ' | fin |
| Fijian | 'FJI ' | fij |
| Dutch (Flemish) | 'FLE ' | vls |
| Fe’fe’ | 'FMP ' | fmp |
| Forest Enets | 'FNE ' | enf |
| Fon | 'FON ' | fon |
| Faroese | 'FOS ' | fao |
| French | 'FRA ' | fra |
| Cajun French | 'FRC ' | frc |
| Frisian | 'FRI ' | fry |
| Friulian | 'FRL ' | fur |
| Arpitan | 'FRP ' | frp |
| Futa | 'FTA ' | fuf |
| Fulah | 'FUL ' | ful |
| Nigerian Fulfulde | 'FUV ' | fuv |
| Ga | 'GAD ' | gaa |
| Scottish Gaelic | 'GAE ' | gla |
| Gagauz | 'GAG ' | gag |
| Galician | 'GAL ' | glg |
| Garshuni | 'GAR ' |  |
| Garhwali | 'GAW ' | gbm |
| Geez | 'GEZ ' | gez |
| Githabul | 'GIH ' | gih |
| Gilyak | 'GIL ' | niv |
| Kiribati (Gilbertese) | GIL0 | gil |
| Kpelle (Guinea) | 'GKP ' | gkp |
| Gilaki | 'GLK ' | glk |
| Gumuz | 'GMZ ' | guk |
| Gumatj | 'GNN ' | gnn |
| Gogo | 'GOG ' | gog |
| Gondi | 'GON ' | gon |
| Greenlandic | 'GRN ' | kal |
| Garo | 'GRO ' | grt |
| Guarani | 'GUA ' | grn |
| Wayuu | 'GUC ' | guc |
| Gupapuyngu | 'GUF ' | guf |
| Gujarati | 'GUJ ' | guj |
| Gusii | 'GUZ ' | guz |
| Haitian (Haitian Creole) | 'HAI ' | hat |
| Haida | 'HAI0' | hai, hax, hdn |
| Halam (Falam Chin) | 'HAL ' | cfm |
| Harauti | 'HAR ' | hoj |
| Hausa | 'HAU ' | hau |
| Hawaiian | 'HAW ' | haw |
| Haya | 'HAY ' | hay |
| Hazaragi | 'HAZ ' | haz |
| Hammer-Banna | 'HBN ' | amf |
| Heiltsuk | 'HEI ' | hei |
| Herero | 'HER ' | her |
| Hiligaynon | 'HIL ' | hil |
| Hindi | 'HIN ' | hin |
| High Mari | 'HMA ' | mrj |
| A-Hmao | 'HMD ' | hmd |
| Hmong | 'HMN ' | hmn |
| Hiri Motu | 'HMO ' | hmo |
| Hmong Shuat | 'HMZ ' | hmz |
| Hindko | 'HND ' | hno, hnd |
| Ho | 'HO ' | hoc |
| Harari | 'HRI ' | har |
| Croatian | 'HRV ' | hrv |
| Hungarian | 'HUN ' | hun |
| Halkomelem | 'HUR ' | hur |
| Armenian | 'HYE ' | hye, hyw |
| Armenian East | HYE0 | hye |
| Iban | 'IBA ' | iba |
| Ibibio | 'IBB ' | ibb |
| Igbo | 'IBO ' | ibo |
| Ido | 'IDO ' | ido |
| Ijo languages | 'IJO ' | iby, ijc, ije, ijn, ijo, ijs, nkx, okd, okr, orr |
| Interlingue | 'ILE ' | ile |
| Ilokano | 'ILO ' | ilo |
| Interlingua | 'INA ' | ina |
| Indonesian | 'IND ' | ind |
| Ingush | 'ING ' | inh |
| Inuktitut | 'INU ' | iku |
| Nunavik Inuktitut | 'INUK' | ike, iku |
| Inupiat | 'IPK ' | ipk |
| Phonetic transcription—IPA conventions | IPPH | Cf. BCP 47 variant subtag “fonipa” (see https://www.iana.org/assignments/lang-subtags-templates/fonipa.txt) |
| Irish | 'IRI ' | gle |
| Irish Traditional | 'IRT ' | ghc, gle |
| Irula | 'IRU ' | iru |
| Icelandic | 'ISL ' | isl |
| Inari Sami | 'ISM ' | smn |
| Italian | 'ITA ' | ita |
| Hebrew | 'IWR ' | heb |
| Jamaican Creole | 'JAM ' | jam |
| Japanese | 'JAN ' | jpn |
| Javanese | 'JAV ' | jav |
| Lojban | 'JBO ' | jbo |
| Krymchak | 'JCT ' | jct |
| Judeo-Tat | 'JDT ' | jdt |
| Yiddish | 'JII ' | yid |
| Ladino | 'JUD ' | lad |
| Jula | 'JUL ' | dyu |
| Kabardian | 'KAB ' | kbd |
| Kabyle | KAB0 | kab |
| Kachchi | 'KAC ' | kfr |
| Kalenjin | 'KAL ' | kln |
| Kannada | 'KAN ' | kan |
| Karachay | 'KAR ' | krc |
| Georgian | 'KAT ' | kat |
| Kawi (Old Javanese) | 'KAW ' | kaw |
| Kazakh | 'KAZ ' | kaz |
| Kadiwéu | 'KBC ' | kbc |
| Makonde | 'KDE ' | kde |
| Kabuverdianu (Crioulo) | 'KEA ' | kea |
| Kebena | 'KEB ' | ktb |
| Kekchi | 'KEK ' | kek |
| Khutsuri Georgian | 'KGE ' | kat |
| Kube | 'KGF ' | kgf |
| Khakass | 'KHA ' | kjh |
| Khanty-Kazim | 'KHK ' | kca |
| Khmer | 'KHM ' | khm |
| Khanty-Shurishkar | 'KHS ' | kca |
| Khamti Shan | 'KHT ' | kht |
| Khanty-Vakhi | 'KHV ' | kca |
| Khowar | 'KHW ' | khw |
| Kikuyu (Gikuyu) | 'KIK ' | kik |
| Kirghiz (Kyrgyz) | 'KIR ' | kir |
| Kisii | 'KIS ' | kqs, kss |
| Kirmanjki | 'KIU ' | kiu |
| Southern Kiwai | 'KJD ' | kjd |
| Khinalug | 'KJJ ' | kjj |
| Eastern Pwo Karen | 'KJP ' | kjp |
| Bumthangkha | 'KJZ ' | kjz |
| Kokni | 'KKN ' | kex |
| Kalmyk | 'KLM ' | xal |
| Kamba | 'KMB ' | kam |
| Kâte | 'KMG ' | kmg |
| Kumaoni | 'KMN ' | kfy |
| Komo | 'KMO ' | kmw |
| Komso | 'KMS ' | kxc |
| Khorasani Turkic | 'KMZ ' | kmz |
| Kanuri | 'KNR ' | kau |
| Kodagu | 'KOD ' | kfa |
| Korean Old Hangul | 'KOH ' | kor, okm |
| Konkani | 'KOK ' | kok |
| Komi | 'KOM ' | kom |
| Kikongo | 'KON ' | ktu |
| Kongo | KON0 | kon |
| Komi-Permyak | 'KOP ' | koi |
| Korean | 'KOR ' | kor |
| Kosraean | 'KOS ' | kos |
| Komi-Zyrian | 'KOZ ' | kpv |
| Kpelle | 'KPL ' | kpe |
| Krio | 'KRI ' | kri |
| Karakalpak | 'KRK ' | kaa |
| Karelian | 'KRL ' | krl |
| Karaim | 'KRM ' | kdr |
| Karen | 'KRN ' | blk, bwe, eky, ghk, jkm, jkp, kar, kjp, kjt, ksw, kvl, kvq, kvt, kvu, kvy, kxf, kxk, kyu, pdu, pwo, pww, wea |
| Koorete | 'KRT ' | kqy |
| Kashmiri | 'KSH ' | kas |
| Ripuarian | KSH0 | ksh |
| Khasi | 'KSI ' | kha |
| Kildin Sami | 'KSM ' | sjd |
| Khamyang | 'KSU ' | ksu |
| S’gaw Karen | 'KSW ' | ksw |
| Kuanyama | 'KUA ' | kua |
| Kui | 'KUI ' | kxu |
| Kulvi | 'KUL ' | kfx |
| Kumyk | 'KUM ' | kum |
| Kurdish | 'KUR ' | kur |
| Kurukh | 'KUU ' | kru |
| Kuy | 'KUY ' | kdt |
| Geba Karen | 'KVQ ' | kvq |
| Kwakʼwala | 'KWK ' | kwk |
| Koryak | 'KYK ' | kpy |
| Western Kayah | 'KYU ' | kyu |
| Ladin | 'LAD ' | lld |
| Lahuli | 'LAH ' | bfu |
| Lak | 'LAK ' | lbe |
| Lambani | 'LAM ' | lmn |
| Lao | 'LAO ' | lao |
| Latin | 'LAT ' | lat |
| Laz | 'LAZ ' | lzz |
| L-Cree | 'LCR ' | crm |
| Ladakhi | 'LDK ' | lbj |
| Lelemi | 'LEF ' | lef |
| Lezgi | 'LEZ ' | lez |
| Ligurian | 'LIJ ' | lij |
| Limburgish | 'LIM ' | lim |
| Lingala | 'LIN ' | lin |
| Lisu | 'LIS ' | lis |
| Liv | 'LIV ' | liv |
| Lampung | 'LJP ' | ljp |
| Laki | 'LKI ' | lki |
| Low Mari | 'LMA ' | mhr |
| Limbu | 'LMB ' | lif |
| Lombard | 'LMO ' | lmo |
| Lomwe | 'LMW ' | ngl |
| Loma | 'LOM ' | lom |
| Lipo | 'LPO ' | lpo |
| Luri | 'LRC ' | lrc, luz, bqi, zum |
| Lower Sorbian | 'LSB ' | dsb |
| Lule Sami | 'LSM ' | smj |
| Lithuanian | 'LTH ' | lit |
| Luxembourgish | 'LTZ ' | ltz |
| Luba-Lulua | 'LUA ' | lua |
| Luba-Katanga | 'LUB ' | lub |
| Ganda | 'LUG ' | lug |
| Luyia | 'LUH ' | luy |
| Luo | 'LUO ' | luo |
| Lushootseed | 'LUT ' | lut |
| Latvian | 'LVI ' | lav |
| Madura | 'MAD ' | mad |
| Magahi | 'MAG ' | mag |
| Marshallese | 'MAH ' | mah |
| Majang | 'MAJ ' | mpe |
| Makhuwa | 'MAK ' | vmw |
| Malayalam | 'MAL ' | mal |
| Mam | 'MAM ' | mam |
| Mansi | 'MAN ' | mns |
| Mapudungun | 'MAP ' | arn |
| Marathi | 'MAR ' | mar |
| Marwari | 'MAW ' | mwr, dhd, rwr, mve, wry, mtr, swv |
| Mbundu | 'MBN ' | kmb |
| Mbo | 'MBO ' | mbo |
| Manchu | 'MCH ' | mnc |
| Moose Cree | 'MCR ' | crm |
| Mende | 'MDE ' | men |
| Mandar | 'MDR ' | mdr |
| Me’en | 'MEN ' | mym |
| Meru | 'MER ' | mer |
| Mano | 'MEV ' | mev |
| Pattani Malay | 'MFA ' | mfa |
| Morisyen | 'MFE ' | mfe |
| Minangkabau | 'MIN ' | min |
| Mizo | 'MIZ ' | lus |
| Macedonian | 'MKD ' | mkd |
| Makasar | 'MKR ' | mak |
| Kituba | 'MKW ' | mkw |
| Male | 'MLE ' | mdy |
| Malagasy | 'MLG ' | mlg |
| Malinke | 'MLN ' | mlq |
| Malayalam Reformed | 'MLR ' | mal |
| Malay | 'MLY ' | msa |
| Mandinka | 'MND ' | mnk |
| Mongolian | 'MNG ' | mon |
| Manipuri | 'MNI ' | mni |
| Maninka | 'MNK ' | man, mnk, myq, mku, msc, emk, mwk, mlq |
| Manx | 'MNX ' | glv |
| Mohawk | 'MOH ' | moh |
| Moksha | 'MOK ' | mdf |
| Moldavian | 'MOL ' | mol |
| Mon | 'MON ' | mnw |
| Thailand Mon | 'MONT' | mnwSee https://www.unicode.org/L2/L2020/20163-arakanese-mon.pdf |
| Moroccan | 'MOR ' |  |
| Mossi | 'MOS ' | mos |
| Maori | 'MRI ' | mri |
| Maithili | 'MTH ' | mai |
| Maltese | 'MTS ' | mlt |
| Mundari | 'MUN ' | unr |
| Muscogee | 'MUS ' | mus |
| Mirandese | 'MWL ' | mwl |
| Hmong Daw | 'MWW ' | mww |
| Mayan | 'MYN ' | acr, agu, caa, cac, cak, chf, ckz, cob, ctu, emy, hus, itz, ixl, jac, kek, kjb, knj, lac, mam, mhc, mop, myn, poc, poh, quc, qum, quv, toj, ttc, tzh, tzj, tzo, usp, yua |
| Mazanderani | 'MZN ' | mzn |
| Naga-Assamese | 'NAG ' | nag |
| Nahuatl | 'NAH ' | azd, azn, azz, nah, naz, nch, nci, ncj, ncl, ncx, ngu, nhc, nhe, nhg, nhi, nhk, nhm, nhn, nhp, nhq, nht, nhv, nhw, nhx, nhy, nhz, nlv, npl, nsu, nuz |
| Nanai | 'NAN ' | gld |
| Neapolitan | 'NAP ' | nap |
| Naskapi | 'NAS ' | nsk |
| Nauruan | 'NAU ' | nau |
| Navajo | 'NAV ' | nav |
| N-Cree | 'NCR ' | csw |
| Ndebele | 'NDB ' | nbl, nde |
| Ndau | 'NDC ' | ndc |
| Ndonga | 'NDG ' | ndo |
| Low Saxon | 'NDS ' | nds |
| Nepali | 'NEP ' | nep |
| Newari | 'NEW ' | new |
| Ngbaka | 'NGA ' | nga |
| Nagari | 'NGR ' |  |
| Norway House Cree | 'NHC ' | csw |
| Nisi | 'NIS ' | dap, njz, tgj |
| Niuean | 'NIU ' | niu |
| Nyankole | 'NKL ' | nyn |
| N’Ko | 'NKO ' | nqo |
| Dutch | 'NLD ' | nld |
| Nimadi | 'NOE ' | noe |
| Nogai | 'NOG ' | nog |
| Numanggang | 'NOP ' | nop |
| Norwegian | 'NOR ' | nob |
| Novial | 'NOV ' | nov |
| Northern Sami | 'NSM ' | sme |
| Northern Sotho | 'NSO ' | nso |
| Northern Tai | 'NTA ' | nod |
| Esperanto | 'NTO ' | epo |
| Nuu-chah-nulth | 'NUK ' | nuk |
| Nyamwezi | 'NYM ' | nym |
| Norwegian Nynorsk (Nynorsk, Norwegian) | 'NYN ' | nno |
| Mbembe Tigon | 'NZA ' | nza |
| Occitan | 'OCI ' | oci |
| Oji-Cree | 'OCR ' | ojs |
| Ojibway | 'OJB ' | oji |
| Oneida | 'ONE ' | one |
| Onondaga | 'ONO ' | ono |
| Odia | 'ORI ' | ori |
| Oromo | 'ORO ' | orm |
| Ossetian | 'OSS ' | oss |
| Palestinian Aramaic | 'PAA ' | sam |
| Pangasinan | 'PAG ' | pag |
| Pali | 'PAL ' | pli |
| Pampangan | 'PAM ' | pam |
| Punjabi | 'PAN ' | pan |
| Palpa | 'PAP ' | plp |
| Papiamentu | PAP0 | pap |
| Pashto | 'PAS ' | pus |
| Palauan | 'PAU ' | pau |
| Bouyei | 'PCC ' | pcc |
| Picard | 'PCD ' | pcd |
| Pennsylvania German | 'PDC ' | pdc |
| Polytonic Greek | 'PGR ' | ell |
| Phake | 'PHK ' | phk |
| Norfolk | 'PIH ' | pih |
| Filipino | 'PIL ' | fil |
| Palaung | 'PLG ' | pce, rbb, pll |
| Pilagá | 'PLG0' | plg |
| Polish | 'PLK ' | pol |
| Piemontese | 'PMS ' | pms |
| Western Panjabi | 'PNB ' | pnb |
| Pocomchi | 'POH ' | poh |
| Pohnpeian | 'PON ' | pon |
| Provençal / Old Provençal | 'PRO ' | pro |
| Portuguese | 'PTG ' | por |
| Western Pwo Karen | 'PWO ' | pwo |
| Chin | 'QIN ' | bgr, biu, cek, cey, cfm, cbl, cka, ckn, clj, clt, cmr, cnb, cnh, cnk, cnw, csh, csj, csv, csy, ctd, cth, czt, dao, gnb, hlt, hmr, hra, lus, mrh, mwq, pck, pkh, pub, ral, rtc, sch, sez, shl, smt, tcp, tcz, vap, weu, zom, zyp |
| K’iche’ | 'QUC ' | quc |
| Quechua (Bolivia) | 'QUH ' | quh |
| Quechua | 'QUZ ' | quz |
| Quechua (Ecuador) | 'QVI ' | qvi |
| Quechua (Peru) | 'QWH ' | qwh |
| Rajasthani | 'RAJ ' | raj |
| Rarotongan | 'RAR ' | rar |
| Russian Buriat | 'RBU ' | bxr |
| R-Cree | 'RCR ' | atj |
| Rejang | 'REJ ' | rej |
| Riang | 'RIA ' | ria |
| Rohingya | 'RHG ' | rhg |
| Tarifit | 'RIF ' | rif |
| Ritarungo | 'RIT ' | rit |
| Arakwal | 'RKW ' | rkw |
| Romansh | 'RMS ' | roh |
| Vlax Romani | 'RMY ' | rmy |
| Romanian | 'ROM ' | ron |
| Romany | 'ROY ' | rom |
| Rusyn | 'RSY ' | rue |
| Rotuman | 'RTM ' | rtm |
| Kinyarwanda | 'RUA ' | kin |
| Rundi | 'RUN ' | run |
| Aromanian | 'RUP ' | rup |
| Russian | 'RUS ' | rus |
| Sadri | 'SAD ' | sck |
| Sanskrit | 'SAN ' | san |
| Sasak | 'SAS ' | sas |
| Santali | 'SAT ' | sat |
| Sayisi | 'SAY ' | chp |
| Sicilian | 'SCN ' | scn |
| Scots | 'SCO ' | sco |
| North Slavey | 'SCS ' | scs |
| Seneca | 'SEE ' | see |
| Sekota | 'SEK ' | xan |
| Selkup | 'SEL ' | sel |
| Small Flowery Miao | 'SFM ' | sfm |
| Old Irish | 'SGA ' | mga, sga |
| Sango | 'SGO ' | sag |
| Samogitian | 'SGS ' | sgs |
| Tachelhit | 'SHI ' | shi |
| Shan | 'SHN ' | shn |
| Sibe | 'SIB ' | sjo |
| Sidamo | 'SID ' | sid |
| Silte Gurage | 'SIG ' | xst, stv, wle |
| Epena | 'SJA ' | sja |
| Pite Sami | 'SJE ' | sje |
| Ume Sami | 'SJU ' | sju |
| Skolt Sami | 'SKS ' | sms |
| Slovak | 'SKY ' | slk |
| Slavey | 'SLA ' | den, scs, xsl |
| Slovenian | 'SLV ' | slv |
| Somali | 'SML ' | som |
| Samoan | 'SMO ' | smo |
| Sena | 'SNA ' | seh |
| Shona | SNA0 | sna |
| Sindhi | 'SND ' | snd |
| Sinhala (Sinhalese) | 'SNH ' | sin |
| Soninke | 'SNK ' | snk |
| Sodo Gurage | 'SOG ' | gru |
| Songe | 'SOP ' | sop |
| Southern Sotho | 'SOT ' | sot |
| Albanian | 'SQI ' | sqi |
| Serbian | 'SRB ' | cnr, srp |
| Sardinian | 'SRD ' | srd |
| Saraiki | 'SRK ' | skr |
| Serer | 'SRR ' | srr |
| South Slavey | 'SSL ' | xsl |
| Southern Sami | 'SSM ' | sma |
| Straits Salish | 'STR ' | str |
| Saterland Frisian | 'STQ ' | stq |
| Sukuma | 'SUK ' | suk |
| Sundanese | 'SUN ' | sun |
| Suri | 'SUR ' | suq |
| Svan | 'SVA ' | sva |
| Swedish | 'SVE ' | swe |
| Swadaya Aramaic | 'SWA ' | aii |
| Swahili | 'SWK ' | swa |
| Swati | 'SWZ ' | ssw |
| Sutu | 'SXT ' | ngo, xnj, xnq |
| Upper Saxon | 'SXU ' | sxu |
| Sylheti | 'SYL ' | syl |
| Syriac | 'SYR ' | aii, amw, cld, syc, syr, tru |
| Syriac, Estrangela script-variant (equivalent to ISO 15924 'Syre') | SYRE | syc, syr |
| Syriac, Western script-variant (equivalent to ISO 15924 'Syrj') | SYRJ | syc, syr |
| Syriac, Eastern script-variant (equivalent to ISO 15924 'Syrn') | SYRN | syc, syr |
| Silesian | 'SZL ' | szl |
| Tabasaran | 'TAB ' | tab |
| Tajiki | 'TAJ ' | tgk |
| Tamil | 'TAM ' | tam |
| Tamasheq | 'TAQ ' | taqSee also 'TMH ' |
| Tatar | 'TAT ' | tat |
| Tobo | 'TBV ' | tbv |
| TH-Cree | 'TCR ' | cwd |
| Emberá-Tadó | 'TDC ' | tdc |
| Dehong Dai | 'TDD ' | tdd |
| Telugu | 'TEL ' | tel |
| Tetum | 'TET ' | tet |
| Tagalog | 'TGL ' | tgl |
| Tongan | 'TGN ' | ton |
| Tigre | 'TGR ' | tig |
| Tigrinya | 'TGY ' | tir |
| Thai | 'THA ' | tha |
| Tahitian | 'THT ' | tah |
| Thompson | 'THP ' | thp |
| Tahaggart Tamahaq | 'THV ' | thvSee also 'TMH ' |
| Tayart Tamajeq | 'THZ ' | thzSee also 'TMH ' |
| Tibetan | 'TIB ' | bod |
| Tiv | 'TIV ' | tiv |
| Tai Laing | 'TJL ' | tjl |
| Turkmen | 'TKM ' | tuk |
| Tlingit | 'TLI ' | tli |
| Tamashek | 'TMH ' | taq, thv, thz, tmh, ttqSee also 'TAQ ', 'THV ', 'THZ ', 'TTQ ' |
| Talysh | 'TLY ' | tly |
| Temne | 'TMN ' | tem |
| Tswana | 'TNA ' | tsn |
| Tundra Enets | 'TNE ' | enh |
| Tonga | 'TNG ' | toi |
| Todo | 'TOD ' | xal |
| Toma | TOD0 | tod |
| Tok Pisin | 'TPI ' | tpi |
| Turkish | 'TRK ' | tur |
| Tsonga | 'TSG ' | tso |
| Tshangla | 'TSJ ' | tsj |
| Tawallammat Tamajaq | 'TTQ ' | ttqSee also 'TMH ' |
| Turoyo Aramaic | 'TUA ' | tru |
| Tulu | 'TUL ' | tcy |
| Tumbuka | 'TUM ' | tum |
| Tuscarora | 'TUS ' | tus |
| Tuvin | 'TUV ' | tyv |
| Tuvalu | 'TVL ' | tvl |
| Twi | 'TWI ' | twi |
| Tày | 'TYZ ' | tyz |
| Tamazight | 'TZM ' | tzm |
| Tzotzil | 'TZO ' | tzo |
| Udi | 'UDI ' | udi |
| Udmurt | 'UDM ' | udm |
| Ukrainian | 'UKR ' | ukr |
| Umbundu | 'UMB ' | umb |
| Uralic Phonetic Alphabet | 'UPPH' | Cf. BCP 47 variant subtag “fonupa” (see https://www.iana.org/assignments/lang-subtags-templates/fonupa.txt) |
| Urdu | 'URD ' | urd |
| Upper Sorbian | 'USB ' | hsb |
| Uyghur | 'UYG ' | uig |
| Uzbek | 'UZB ' | uzb |
| Venetian | 'VEC ' | vec |
| Venda | 'VEN ' | ven |
| Vietnamese | 'VIT ' | vie |
| Volapük | 'VOL ' | vol |
| Võro | 'VRO ' | vro |
| Wa | 'WA ' | wbm |
| Wagdi | 'WAG ' | wbr |
| Waray-Waray | 'WAR ' | war |
| Wakhi | 'WBL ' | wbl |
| Waci Gbe | 'WCI ' | wci |
| West-Cree | 'WCR ' | crk |
| Wendat | 'WDT ' | wdt |
| Welsh | 'WEL ' | cym |
| Wolof | 'WLF ' | wol |
| Walloon | 'WLN ' | wln |
| Mewati | 'WTM ' | wtm |
| Wyandot | 'WYN ' | wyn |
| Lü | 'XBD ' | khb |
| Xhosa | 'XHS ' | xho |
| Minjangbal | 'XJB ' | xjb |
| Khengkha | 'XKF ' | xkf |
| Soga | 'XOG ' | xog |
| Kpelle (Liberia) | 'XPE ' | xpe |
| Bette Kuruma | 'XUB ' | xub |
| Jennu Kuruma | 'XUJ ' | xuj |
| Sakha | 'YAK ' | sah |
| Yao | 'YAO ' | yao |
| Yapese | 'YAP ' | yap |
| Yoruba | 'YBA ' | yor |
| Y-Cree | 'YCR ' | crj, crk, crl |
| Gepo | 'YGP ' | ygp |
| Yi Classic | 'YIC ' |  |
| Yi Modern | 'YIM ' | iii |
| Aluo | 'YNA ' | yna |
| Havasupai-Walapai-Yavapai | 'YUF ' | yuf |
| Wuding-Luquan Yi | 'YWQ ' | ywq |
| Zealandic | 'ZEA ' | zea |
| Standard Moroccan Tamazight | 'ZGH ' | zgh |
| Zhuang | 'ZHA ' | zha |
| Chinese, Traditional, Hong Kong SAR | 'ZHH ' | zho |
| Chinese, Phonetic | 'ZHP ' | zho |
| Chinese, Simplified | 'ZHS ' | zho |
| Chinese, Traditional | 'ZHT ' | zho |
| Chinese, Traditional, Macao SAR | 'ZHTM' | zho |
| Zande | 'ZND ' | zne |
| Zulu | 'ZUL ' | zul |
| Zazaki | 'ZZA ' | zza |
