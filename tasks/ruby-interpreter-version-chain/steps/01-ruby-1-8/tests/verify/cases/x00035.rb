begin
  exit(3)
rescue SystemExit => e
  p e.status
end
p :after
