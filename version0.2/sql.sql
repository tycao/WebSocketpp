create table if not exists `n_m_user_info` (
	type varchar(10),
	content varchar(1000),
	cid varchar(30) PRIMARY KEY,
	login_time varchar(100),
	logout_time varchar(100)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;

create table if not exists `n_m_user_info_his` (
	type varchar(10),
	content varchar(1000),
	cid varchar(30) PRIMARY KEY,
	login_time varchar(100),
	logout_time varchar(100)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;