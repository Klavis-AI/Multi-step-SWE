rest = nil; x = nil; a = nil; y = nil
begin
  case []
  in [1, 2]
    p :matched
  else
    p :no_match
  end
rescue Exception => e
  p e.class
end
