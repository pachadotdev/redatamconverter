# devtools::install()
# devtools::load_all()
# variables <- read_redatam("dev/AR-BASE EPH FINAL.dic")

variables <- redatam::read_redatam("dev/AR-BASE EPH FINAL.dic")
print(variables)
names(variables)