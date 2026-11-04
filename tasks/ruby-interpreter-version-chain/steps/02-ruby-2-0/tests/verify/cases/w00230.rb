begin
  p(({"k" => 1}).map { |k, v| [k, v] })
rescue Exception => e
  p e.class
end
