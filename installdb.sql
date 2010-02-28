use mysql;

create function dom_xpath returns string soname 'lib_mysqludf_dom.so';
create function dom_extract_node returns string soname 'lib_mysqludf_dom.so';
create function dom_delete_node returns string soname 'lib_mysqludf_dom.so';
create function dom_replace_node returns string soname 'lib_mysqludf_dom.so';
create function dom_append_child returns string soname 'lib_mysqludf_dom.so';
