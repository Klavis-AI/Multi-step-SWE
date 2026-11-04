begin
  p({"k" => [1, :v], :"9x" => 2})
rescue Exception => e
  p e.class
end
