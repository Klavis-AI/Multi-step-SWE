rest = nil; x = nil; a = nil; y = nil
begin
  case {a: 1}
  in {a: 1}
    p :matched
  else
    p :no_match
  end
rescue Exception => e
  p e.class
end
