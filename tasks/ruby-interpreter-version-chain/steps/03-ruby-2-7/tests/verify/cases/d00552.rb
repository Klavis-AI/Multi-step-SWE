rest = nil; x = nil; a = nil; y = nil
begin
  case [1, 2, 3]
  in [1, *rest]
    p :matched
  else
    p :no_match
  end
rescue Exception => e
  p e.class
end
