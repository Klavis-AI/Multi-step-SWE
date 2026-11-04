begin
  p(({"k" => 1}).to_h)
rescue Exception => e
  p e.class
end
