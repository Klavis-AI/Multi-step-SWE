CREATE PROCEDURE q3_pci_p(x int) LANGUAGE plpgsql AS $$ BEGIN NULL; END $$;
CREATE FUNCTION q3_pci_f(x int) RETURNS int LANGUAGE sql AS $$ SELECT x $$;
SELECT routine_name, routine_type, data_type FROM information_schema.routines
  WHERE routine_name IN ('q3_pci_p','q3_pci_f') ORDER BY routine_name;
SELECT routine_name, parameter_name, parameter_mode FROM information_schema.parameters p
  JOIN information_schema.routines r USING (specific_name)
  WHERE routine_name IN ('q3_pci_p','q3_pci_f') ORDER BY routine_name, ordinal_position;
DROP PROCEDURE q3_pci_p(int);
DROP FUNCTION q3_pci_f(int);
