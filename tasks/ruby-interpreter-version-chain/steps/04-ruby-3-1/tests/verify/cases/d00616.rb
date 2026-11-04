rest = nil; x = nil; a = nil; y = nil
begin
  case {a: 1, b: 2}
  in {a:}
    p :matched
  else
    p :no_match
  end
rescue Exception => e
  p e.class
end
