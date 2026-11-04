CREATE PROCEDURE q3_pov(x int) LANGUAGE plpgsql AS $$ BEGIN RAISE NOTICE 'int %', x; END $$;
CREATE PROCEDURE q3_pov(x text) LANGUAGE plpgsql AS $$ BEGIN RAISE NOTICE 'text %', x; END $$;
CALL q3_pov(1);
CALL q3_pov('a');
CALL q3_pov(1.5);
DROP PROCEDURE q3_pov(int);
DROP PROCEDURE q3_pov(text);
