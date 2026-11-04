SELECT x.v FROM xmltable('/r/row' PASSING XMLPARSE(DOCUMENT '<r><row><v>abc</v></row></r>')
  COLUMNS v int PATH 'v') AS x;
SELECT x.v FROM xmltable('///' PASSING XMLPARSE(DOCUMENT '<r/>') COLUMNS v text PATH '.') AS x;
SELECT x.v FROM xmltable('/r' PASSING 'not xml at all' COLUMNS v text PATH '.') AS x;
SELECT xmltable('/r' PASSING XMLPARSE(DOCUMENT '<r/>'));
