begin
  x = 3
  case 3
  in ^x then p :pinned
  else p :no
  end
rescue Exception => e
  p e.class
end
