begin
  p({:a => 1, "b" => 2, 3 => 4})
rescue Exception => e
  p e.class
end
