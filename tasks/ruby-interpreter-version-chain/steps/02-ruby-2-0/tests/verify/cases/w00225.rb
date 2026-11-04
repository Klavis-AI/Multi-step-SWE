begin
  p(({"k" => 1}).to_a)
rescue Exception => e
  p e.class
end
