begin
  p(({"k" => 1}).invert)
rescue Exception => e
  p e.class
end
