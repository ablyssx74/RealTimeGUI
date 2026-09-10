name			$(TARGET)
version			$(VERSION)-$(REVISION)
architecture	$(ARCH)
summary 		"$(SUMMARY)"
description 	"$(DESCRIPTION)"
packager		"$(PACKAGER)"
vendor			"$(VENDOR)"
licenses {
	"$(LICENSE)"
}
copyrights {
	"$(YEAR) $(AUTHOR)"
}
provides {
	$(TARGET) = $(VERSION)-$(REVISION)
}
requires {
	$(REQUIRES)
}
urls {
	"$(URLS)"
}
