#include"k05.h"
//{{{ int __init minedev_init(void)
int __init minedev_init(void)
{
	int res,retval;
	int devno;
	mem_class=NULL;
	devno=MKDEV(DRV_MAJOR,DRV_MINOR);
	mp=(char*)kmalloc(K_BUFFER_SIZE,GFP_KERNEL);
	if(mp==NULL)
		printk("<1>kmalloc error!\n");
	else
		memset(mp,0,K_BUFFER_SIZE);
	mp[0]=1;//允许用户进程发送指令。
	tp=(char*)vmalloc(K_BUFFER_SIZE);
	if(tp==NULL)
		printk("<1>vmalloc error!\n");
	else
		memset(tp,0,K_BUFFER_SIZE);
	res=register_chrdev(DRV_MAJOR,drv_name,&mem_fops);//注册字符设备
	if(res)
	{
		printk("<1>register char dev error\n");
		goto reg_err01;
	}
	mem_class=kzalloc(sizeof(*mem_class),GFP_KERNEL);//实体话设备类
	if(IS_ERR(mem_class))
	{
		kfree(mem_class);
		mem_class=NULL;
		printk("<1>kzalloc error\n");
		goto reg_err02;
	}
	mem_class->name=drv_name;
	mem_class->owner=THIS_MODULE;
	mem_class->class_release=class_create_release;
	retval=class_register(mem_class);//注册设备类
	if(retval)
	{
		kfree(mem_class);
		printk("<1>class_register error\n");
		goto reg_err02;
	}
	device_create(mem_class,NULL,devno,NULL,drv_name);//注册设备文件系统，并建立节点。
	printk("<1>device create successful!!!\n");
	kv1.pin=1;//指定mp作为输入输出缓冲区。
	return 0;
reg_err02:
	unregister_chrdev(DRV_MAJOR,drv_name);//删除字符设备。
reg_err01:
	if(mp!=NULL)
	{	kfree(mp);mp=NULL;}
	if(tp!=NULL)
	{	vfree(tp);tp=NULL;}
	suc=1;
	return -1;	
}//}}}
//{{{ void __exit minedev_exit(void)
void __exit minedev_exit(void)
{
	if(suc!=0)
		return;
	unregister_chrdev(DRV_MAJOR,drv_name);
	device_destroy(mem_class,MKDEV(DRV_MAJOR,DRV_MINOR));
	if(mem_class!=NULL && (!IS_ERR(mem_class)))
		class_unregister(mem_class);
	if(mp!=NULL)
		kfree(mp);
	if(tp!=NULL)
		vfree(tp);
	printk("<1>module eixt ok!\n");
}//}}}
//{{{ int mem_open(struct inode *ind,struct file *filp)
int mem_open(struct inode *ind,struct file *filp)
{
	try_module_get(THIS_MODULE);	//引用计数增加
	printk("<1>device open success!\n");
	return 0;
}//}}}
//{{{ int mem_release(struct inode *ind,struct file *filp)
int mem_release(struct inode *ind,struct file *filp)
{
	module_put(THIS_MODULE);	//计数器减1
	printk("<1>device release success!\n");
	return 0;
}//}}}
//{{{ void class_create_release(struct class *cls)
void class_create_release(struct class *cls)
{
	pr_debug("%s called for %s\n",__func__,cls->name);
	kfree(cls);
}//}}}
//{{{ ssize_t mem_read(struct file *filp,char *buf,size_t size,loff_t *fpos)
ssize_t mem_read(struct file *filp,char *buf,size_t size,loff_t *fpos)
{
	int res;
	char *tmp;
	if(mp==NULL || tp==NULL)
	{
		printk("<1>kernel buffer error!\n");
		return 0;
	}
	switch(kv1.pin)
	{
	case 1://mp作为输入输出缓冲
		tmp=mp;
		break;
	case 2://tp作为输入输出缓冲
		tmp=tp;
		break;
	default://pin为其它值时，不进行拷贝操作，直接返回0
		return 0;
	};
	if(size>K_BUFFER_SIZE)
		size=K_BUFFER_SIZE;
	res=copy_to_user(buf,tmp,size);
	if(res==0)
		return size;
	else
		return 0;
}//}}}
//{{{ ssize_t mem_write(struct file *filp,char *buf,size_t size,loff_t *fpos)
ssize_t mem_write(struct file *filp,const char *buf,size_t size,loff_t *fpos)
{
	int res;
	char *tmp;
	if(mp==NULL || tp==NULL)
	{
		printk("<1>kernel buffer error\n");
		return 0;
	}
	switch(kv1.pin)
	{
	case 1://mp
		tmp=mp;
		break;
	case 2://tp
		tmp=tp;
		break;
	default:
		return 0;
	};
	if(size>K_BUFFER_SIZE)
		size=K_BUFFER_SIZE;
	res=copy_from_user(tmp,buf,size);
	if(res==0)
	{
		if(tmp[0]!=0 || tmp[2]==1)
			return size;
		if(kv1.thread_lock==0)
		{
			k_am=(struct KVAR_AM*)tmp;
			k_am->sync=0;k_am->end0=0;
			kv1.pid=k_am->pid;//保存pid
			kv1.snum=k_am->snum;//保存搜索数字。
			if(k_am->cmd==1)//首次查找操作。
			{
				kv1.pin=0;//不再接收用户的输入输出。
				kv1.thread_lock=1;//设置线程锁
				kernel_thread(first_srh,NULL,CLONE_KERNEL);
				return size;
			}
			if(k_am->cmd==2)//再次查询。
			{
				k_tp=(struct KVAR_AM*)tp;
				k_am->end1=0;
				kv1.pin=0;
				kv1.thread_lock=1;
				kernel_thread(next_srh,NULL,CLONE_KERNEL);
				return size;
			}
			if(k_am->cmd==3)//锁定操作
			{//需要保存下代码段和数据段的段长度。
				kv1.t_len=k_am->t_len;
				kv1.d_len=k_am->d_len;
				memset(tp,0,K_BUFFER_SIZE);
				//	kv1.pin=1;//锁定操作要随时准备接收用户输入，终止锁定，所以必须要指定mp为缓冲区。
				kv1.pin=2;//改为使用tp作为缓冲区。
				kv1.thread_lock=1;
				kernel_thread(lock_srh,NULL,CLONE_KERNEL);
				return size;
			}
			if(k_am->cmd==4)//内存检视
			{
				kv1.t_len=k_am->t_len;
				kv1.d_len=k_am->d_len;
				memset(tp,0,K_BUFFER_SIZE);
				kv1.pin=2;//使用tp
				kv1.thread_lock=1;
				kernel_thread(mem_srch,NULL,CLONE_KERNEL);
				return size;
			}
		}
	}
	s_wait_mm(2);
	return size;
}//}}}
//{{{ int first_srh(void *argc)
int first_srh(void *argc)
{
	char *c,*mc,*v;
	int ret,len,i,j,k,m,n;
	struct pid *kpid;
	struct task_struct *t_str;
	struct mm_struct  *mm_str;
	struct vm_area_struct *vadr;
	struct page **pages;
	unsigned int adr;
	union OFFSET *ksa;
	daemonize("ty_thd1");
	v=NULL;
	mc=&mp[d_begin];
	memset(mc,0,dlen);
	ksa=(union OFFSET *)mc;
	kpid=find_get_pid(kv1.pid);
	if(kpid==NULL)
	{
		printk("<1>find_get_pid error\n");
		goto ferr_01;
	}
	t_str=pid_task(kpid,PIDTYPE_PID);
	if(t_str==NULL)
	{
		printk("<1>pid_task error!\n");
		goto ferr_01;
	}
	mm_str=get_task_mm(t_str);
	if(mm_str==NULL)
	{
		printk("<1>get_task_mm error!\n");
		goto ferr_01;
	}
	kv1.seg[0]=mm_str->start_code;//第一个是代码段。
	kv1.seg[1]=mm_str->start_data;//第二个是数据段
	kv1.seg[2]=mm_str->start_brk; //第三个是分配的堆段
	kv1.seg[3]=mm_str->start_stack;//第四个是堆栈段。
	kv1.t_len=mm_str->end_code-mm_str->start_code;//代码段长度
	kv1.d_len=mm_str->end_data-mm_str->start_data;//数据段长度
	printk("first: snum= %d total pages=%lx stack pages=%lx\n",kv1.snum,mm_str->total_vm,mm_str->stack_vm);
	k_am->tol_pg=0;//mm_str->total_vm;
	k_am->fin_pg=0;//这两项是用于进度条显示进度的。
	vadr=mm_str->mmap;//code seg
	i=0;m=0;n=0;
	do
	{
		adr=vadr->vm_start;
		len=vma_pages(vadr);
		if(v!=NULL)
		{
			vfree(v);
			v=NULL;
		}
		v=(char*)vmalloc(sizeof(void*)*(len+1));
		pages=(struct page **)v;
		if(pages==NULL)
		{
			printk("<1>vmalloc error11\n");
			goto ferr_01;
		}
		memset((void*)pages,0,sizeof(void*)*(len+1));
		down_read(&mm_str->mmap_sem);
		ret=get_user_pages(t_str,mm_str,adr,len,0,0,pages,NULL);
		if(ret<=0)
		{
			printk("<1>ret<0\n");
			up_read(&mm_str->mmap_sem);
			goto ferr_01;
		}
		k_am->tol_pg=ret;
		for(k=0;k<ret;k++)
		{
			if(pages[k]==NULL)
			{
				printk("<1>search finished\n");
				break;
			}
			if(k>60000)//规定每个段的页索引不大于6万
			{
				printk("<1>pages counts too large\n");
				break;
			}
			if(m>=AREA_SIZE)
				break;
			c=(char*)kmap(pages[k]);
			//{{{  0x100
			if(kv1.snum<0x100)
			{
				for(j=0;j<4096;j++)
				{
					if(c[j]==kv1.sch[0])
					{//find
						ksa->off=j;ksa->seg=i;ksa->page=k;
						ksa++;
						if((void *)ksa-(void *)mc>7996)
						{
							k_am->tol_pg=ret;
							k_am->fin_pg=k;//n;//传出已经搜索的页数
							s_sync(1);m++;
							ksa=(union OFFSET *)mc;
							memset(mc,0,dlen);
						}
					}
				}
				kunmap(pages[k]);
				page_cache_release(pages[k]);
				continue;
			}//}}}
			//{{{ 0x10000
			if(kv1.snum<0x10000 && kv1.snum>0xff)
			{
				for(j=0;j<4095;j++)
				{
					if((c[j]==kv1.sch[0]) && (c[j+1]==kv1.sch[1]))
					{
						ksa->off=j;ksa->seg=i;ksa->page=k;
						ksa++;
						if((void *)ksa-(void *)mc>7996)
						{
							k_am->fin_pg=k;//传出已经搜索的页数
							s_sync(1);m++;
							ksa=(union OFFSET *)mc;
							memset(mc,0,dlen);
						}
					}
				}
				n++;
				kunmap(pages[k]);
				page_cache_release(pages[k]);
				continue;
			}//}}}
			//{{{ 0x1000000
			if(kv1.snum<0x1000000 && kv1.snum>0xffff)
			{
				for(j=0;j<4094;j++)
				{
					if((c[j]==kv1.sch[0]) && (c[j+1]==kv1.sch[1]) && (c[j+2]==kv1.sch[2]))
					{
						ksa->off=j;ksa->seg=i;ksa->page=k;
						ksa++;
						if((void *)ksa-(void *)mc>7996)
						{
							k_am->fin_pg=k;//传出已经搜索的页数
							s_sync(1);m++;
							ksa=(union OFFSET *)mc;
							memset(mc,0,dlen);
						}
					}
				}
				kunmap(pages[k]);
				page_cache_release(pages[k]);
				continue;
			}//}}}
			//{{{ 0x100000000
			if(kv1.snum>0xffffff)
			{
				for(j=0;j<4093;j++)
				{
					if((c[j]==kv1.sch[0]) && (c[j+1]==kv1.sch[1]) && (c[j+2]==kv1.sch[2]) && (c[j+3]==kv1.sch[3]))
					{
						ksa->off=j;ksa->seg=i;ksa->page=k;
						ksa++;
						if((void *)ksa-(void *)mc>7996)
						{
							k_am->fin_pg=n;//传出已经搜索的页数
							s_sync(1);m++;
							ksa=(union OFFSET *)mc;
							memset(mc,0,dlen);
						}
					}
				}
			}//}}}
			kunmap(pages[k]);
			page_cache_release(pages[k]);
		}
		up_read(&mm_str->mmap_sem);
		pages=NULL;
		printk("<1>seg finished\n");
		if(m>=AREA_SIZE)
		{//最多保存10万个搜索结果。400k
			printk("<1>max counts:%d\n",m*2000);
			goto ferr_01;
		}
		vadr=vadr->vm_next;i++;
		if(vadr==NULL)
			break;
	}while(i<8);
	printk("<1>m=%d n=%d\n",m,n);
/*	s_sync(2);
	kv1.thread_lock=0;
	printk("<1>kernel_thread exit!\n");
	return 0;*/
ferr_01:
	if(v!=NULL)
		vfree(v);
	s_sync(2);
	kv1.thread_lock=0;
	printk("<1>kernel_thread finished!\n");
	return 0;
}//}}}
//{{{ int next_srh(void *argc)
int next_srh(void *argc)
{
 	union OFFSET *kr,*kw;
	char *c,*mc,*md,*v;
	int ret,len,i,j,k;
	struct pid *kpid;
	struct task_struct *t_str;
	struct mm_struct *mm_str;
	struct vm_area_struct *vadr;
	struct page **pages;
	unsigned long adr;
	v=NULL;
	daemonize("ty_thd2");
	kpid=find_get_pid(kv1.pid);
	if(kpid==NULL)
	{
		printk("<1>find_get_pid error\n");
		goto nerr_01;
	}
	t_str=pid_task(kpid,PIDTYPE_PID);
	if(t_str==NULL)
	{
		printk("<1>pid_task error\n");
		goto nerr_01;
	}
	mm_str=get_task_mm(t_str);
	if(mm_str==NULL)
	{
		printk("<1>get_task_mm error\n");
		goto nerr_01;
	}
	kv1.seg[0]=mm_str->start_code;
	kv1.seg[1]=mm_str->start_data;
	kv1.seg[2]=mm_str->start_brk;
	kv1.seg[3]=mm_str->start_stack;
	kv1.t_len=mm_str->end_code-mm_str->start_code;
	kv1.d_len=mm_str->end_data-mm_str->start_data;
	printk("next: total pages=%lx stack pages=%lx\n",mm_str->total_vm,mm_str->stack_vm);
	//c为结果缓冲区，mc为地址集
	c=&tp[d_begin];mc=&mp[d_begin];
	kr=(union OFFSET *)mc;
	kw=(union OFFSET *)c;
	memset(c,0,dlen);
	i=0;
	printk("<1>snum=%d\n",k_am->snum);
	vadr=mm_str->mmap;
	do
	{
		adr=vadr->vm_start;
		len=vma_pages(vadr);
		if(v!=NULL)
		{vfree(v);v=NULL;}
		v=(char*)vmalloc(sizeof(void*)*(len+1));
		pages=(struct page **)v;
		if(pages==NULL)
		{
			printk("<1>vmalloc error12\n");
			goto nerr_01;
		}
		memset((void*)pages,0,sizeof(void*)*(len+1));
		down_read(&mm_str->mmap_sem);
		ret=get_user_pages(t_str,mm_str,adr,len,0,0,pages,NULL);
		if(ret<=0)
		{
			printk("<1>sslsdd\n");
			up_read(&mm_str->mmap_sem);
			goto nerr_01;
		}
		md=mc;//编译时不知为何总有个未初始化的提示。
		j=-1;//i表示段索引，j表示页索引
		while(1)
		{
		//{{{ for---
			if(k_am->end0==1)
			{
				if(kr->ad==0)//没有地址了
				{//这里退出的可能性最大
					if(j!=-1)
					{
						kunmap(pages[j]);
						page_cache_release(pages[j]);
						j=-1;
					}
					up_read(&mm_str->mmap_sem);
					printk("<1>next search finished!\n");
					goto nerr_01;
				}
			}
			if(kr->seg!=i)
			{
				if(j!=-1)
				{
					kunmap(pages[j]);
					page_cache_release(pages[j]);
					j=-1;
				}
				printk("<1>seg end\n");
				break;
			}//完成需要换段
			if(j==-1)
			{
				j=kr->page;
				if(j>=ret)
				{
					printk("<1>error page index\n");
					up_read(&mm_str->mmap_sem);
					goto nerr_01;
				}
				md=(char*)kmap(pages[j]);
			}
			if(j!=kr->page)
			{
				kunmap(pages[j]);
				page_cache_release(pages[j]);
				j=kr->page;
				if(j>=ret)
				{
					printk("<1>error page indexqq\n");
					up_read(&mm_str->mmap_sem);
					goto nerr_01;
				}
				md=(char*)kmap(pages[j]);
			}
			//开始比较
			k=0;
			if(k_am->snum<0x100)
			{
			//	if(k_am->sch[0]==md[kr->off])
				if(md[kr->off]==k_am->sch[0])
					k=1;
				goto n_01;
			}
			if(k_am->snum<0x10000)
			{
				if((k_am->sch[0]==md[kr->off]) &&(k_am->sch[1]==md[kr->off+1]))
					k=1;
				goto n_01;
			}
			if(k_am->snum<0x1000000)
			{
				if((k_am->sch[0]==md[kr->off]) &&(k_am->sch[1]==md[kr->off+1]) && (k_am->sch[2]==md[kr->off+2]))
					k=1;
				goto n_01;
			}
			if((k_am->sch[0]==md[kr->off]) &&(k_am->sch[1]==md[kr->off+1]) && (k_am->sch[2]==md[kr->off+2]) && (k_am->sch[3]==md[kr->off+3]))
				k=1;
n_01:
			if(k==1)//find it
			{
				//printk("<1>find it!\n");
				kw->ad=kr->ad;kw++;
				if(((void*)kw-(void*)c)>7996)
				{
					s_sync(10);
					memset(c,0,dlen);
					kw=(union OFFSET *)c;
				}
			}
			kr++;
			if(((void*)kr-(void*)mc)>7996)//要求用户输入新地址集
			{
				s_sync(12);
				kr=(union OFFSET *)mc;
				//printk("<1>first:0x%x\n",kr->ad);
			}
		//}}}	
		}
		up_read(&mm_str->mmap_sem);
		vadr=vadr->vm_next;
		if(vadr==NULL)
		{
			printk("<1>find enddddd\n");
			break;
		}
		i++;
	}while(i<8);
	printk("<1>ready to exit!\n");
nerr_01:
	if(v!=NULL)
	{
		vfree(v);
		v=NULL;
	}
	k_am->end0=1;
	k_am->end1=0;
	s_sync(12);
	kv1.thread_lock=0;
	printk("<1>next search thread exit....\n");
	return 0;
}//}}}
//{{{ int lock_srh(void *argc)
int lock_srh(void *argc)
{
	struct pid *kpid;
	struct task_struct *t_str;
	struct mm_struct *mm_str;
	struct vm_area_struct *vadr[4];
	struct page **pages;
	//struct KVAR_AM *k_am;
	//union OFFSET *ksa;
	struct KVAR_LOCK *lk;
	char *v,*c;
	int i,j,k,l,m,ret,len;
	unsigned long adr;
	v=NULL;k=0;c=mp;
	daemonize("ty_thd3");
	k_am=(struct KVAR_AM *)mp;
	kpid=find_get_pid(kv1.pid);
	if(kpid==NULL)
	{
		printk("<1>find_get_pid error\n");
		goto lerr_01;
	}
	t_str=pid_task(kpid,PIDTYPE_PID);
	if(t_str==NULL)
	{
		printk("<1>pid_task error\n");
		goto lerr_01;
	}
	mm_str=get_task_mm(t_str);
	if(mm_str==NULL)
	{
		printk("<1>get_task_mm error\n");
		goto lerr_01;
	}
	j=0;k=0;l=0;
	for(i=0;i<8;i++)
	{
		lk=(struct KVAR_LOCK *)&(k_am->ladr[i]);
		if(lk->maxd==0 && lk->mind==0)
			continue;
		if(k==0 && l==0)
		{
			k=lk->offset.seg;
			l=lk->offset.page;
		}
		else
		{
			if((k!=lk->offset.seg) || (l!=lk->offset.page))
			{j=1;break;}
		}
	}
	vadr[0]=mm_str->mmap;
	vadr[1]=mm_str->mmap->vm_next;
	vadr[2]=vadr[1]->vm_next;
	vadr[3]=vadr[2]->vm_next;
	if(j==0)//所有的锁定地址都在同一段内的同一页内，效率最高的锁定
		goto lfast_1;
	while(1)
	{
		for(i=0;i<8;i++)
		{
			lk=(struct KVAR_LOCK *)&(k_am->ladr[i]);
			if(lk->maxd==0 && lk->mind==0)
				continue;
			j=lk->offset.seg;
			if(j<0 || j>3)
				goto lerr_01;
			k=lk->offset.page;
			if(k<0 || k>60000)
				goto lerr_01;
			l=lk->offset.off;
			if(l<0 || l>4095)
				goto lerr_01;
			adr=vadr[j]->vm_start;
			len=vma_pages(vadr[j]);
			if(len<k)
				goto lerr_01;
			if(v!=NULL)
			{
				vfree(v);v=NULL;
			}
			v=(char*)vmalloc(sizeof(void*)*(len+1));
			pages=(struct page **)v;
			if(pages==NULL)
			{
				printk("<1>vmalloc error\n");
				goto lerr_01;
			}
			memset((void*)pages,0,sizeof(void*)*(len+1));
			down_write(&mm_str->mmap_sem);
			ret=get_user_pages(t_str,mm_str,adr,len,0,0,pages,NULL);
			if(ret<=k)
			{
				up_write(&mm_str->mmap_sem);
				goto lerr_01;
			}
			c=kmap(pages[k]);
			if(lk->mind==0)
			{
				if(lk->maxd<0x100)
				{
					if(lk->maxd<c[l])
						c[l]=lk->maxd;
					goto ln_01;
				}
				if(lk->maxd<0x10000)
				{
					if(l>4094)
						goto ln_01;
					m=c[l]+c[l+1]*0x100;
					if(lk->maxd<m)
					{c[l]=lk->mxc[0];c[l+1]=lk->mxc[1];}
					goto ln_01;
				}
				if(lk->maxd<0x1000000)
				{
					if(l>4093)
						goto ln_01;
					m=c[l]+c[l+1]*0x100+c[l+2]*0x10000;
					if(lk->maxd<m)
					{
						c[l]=lk->mxc[0];c[l+1]=lk->mxc[1];
						c[l+2]=lk->mxc[2];
					}
					goto ln_01;
				}
				if(l>4092)
					goto ln_01;
				m=c[l]+c[l+1]*0x100+c[l+2]*0x10000+c[l+3]*0x1000000;
				if(lk->maxd<m)
				{
					c[l]=lk->mxc[0];c[l+1]=lk->mxc[1];
					c[l+2]=lk->mxc[2];c[l+3]=lk->mxc[3];
				}
				goto ln_01;
			}
			if(lk->maxd==0)
			{
				if(lk->mind<0x100)
				{
					if(lk->mind>c[l])
						c[l]=lk->mind;
					goto ln_01;
				}
				if(lk->mind<0x10000)
				{
					if(l>4094)
						goto ln_01;
					m=c[l]+c[l+1]*0x100;
					if(lk->mind>m)
					{c[l]=lk->mic[0];c[l+1]=lk->mic[1];}
					goto ln_01;
				}
				if(lk->mind<0x1000000)
				{
					if(l>4093)
						goto ln_01;
					m=c[l]+c[l+1]*0x100+c[l+2]*0x10000;
					if(lk->mind>m)
					{
						c[l]=lk->mic[0];c[l+1]=lk->mic[1];
						c[l+2]=lk->mic[2];
					}
					goto ln_01;
				}
				if(l>4092)
					goto ln_01;
				m=c[l]+c[l+1]*0x100+c[l+2]*0x10000+c[l+3]*0x1000000;
				if(lk->mind>m)
				{
					c[l]=lk->mic[0];c[l+1]=lk->mic[1];
					c[l+2]=lk->mic[2];c[l+3]=lk->mic[3];
				}
				goto ln_01;
			}
			if(lk->mind<0x100)
			{
				if(lk->mind>c[l] || lk->maxd<c[l])
					c[l]=lk->mind;
				goto ln_01;
			}
			if(lk->mind<0x10000)
			{
				if(l>4094)
					goto ln_01;
				m=c[l]+c[l+1]*0x100;
				if(lk->mind>m || lk->maxd<m)
				{c[l]=lk->mic[0];c[l+1]=lk->mic[1];}
				goto ln_01;
			}
			if(lk->mind<0x1000000)
			{
				if(l>4093)
					goto ln_01;
				m=c[l]+c[l+1]*0x100+c[l+2]*0x10000;
				if(lk->mind>m || lk->maxd<m)
				{
					c[l]=lk->mic[0];c[l+1]=lk->mic[1];
					c[l+2]=lk->mic[2];
				}
				goto ln_01;
			}
			if(l>4092)
				goto ln_01;
			m=c[l]+c[l+1]*0x100+c[l+2]*0x10000+c[l+3]*0x1000000;
			if(lk->mind>m || lk->maxd<m)
			{
				c[l]=lk->mic[0];c[l+1]=lk->mic[1];
				c[l+2]=lk->mic[2];c[l+3]=lk->mic[3];
			}
ln_01:
			kunmap(pages[k]);
			page_cache_release(pages[k]);
			up_write(&mm_str->mmap_sem);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(5*HZ);
		//这里接收缓冲区改为tp
		if(tp[2]==1)//结束
			goto lerr_01;
	}
lfast_1:
	j=0;k=0;l=0;
	for(i=0;i<8;i++)
	{
		lk=(struct KVAR_LOCK *)&(k_am->ladr[i]);
		if(lk->offset.ad!=0)
		{
			l=1;
			j=lk->offset.seg;//segment;
			k=lk->offset.page;//page
			break;
		}
	}
	if(l==0)
		goto lerr_01;
	if(lk->offset.off>4095)
		goto lerr_01;
	if(j<0 || j>3)
		goto lerr_01;
	if(k<0 ||k>60000)
		goto lerr_01;
	adr=vadr[j]->vm_start;
	len=vma_pages(vadr[j]);
	if(len<k)
		goto lerr_01;
	if(v!=NULL)
	{vfree(v);v=NULL;}
	v=(char*)vmalloc(sizeof(void*)*(len+1));
	pages=(struct page **)v;
	if(pages==NULL)
	{
		printk("<1>vmalloc error\n");
		goto lerr_01;
	}/*
	该段代码如果在这个位置，而不在下面的循环内（考虑到效率），则有些
	程序在实时锁定时无法运行，必须调用page_cache_release函数才能正常
	运行。但是调用page_cache_release函数就必须配合重新取得页：再次调用
	get_user_pages,所以，调整后的代码将这段放置到了循环内。	
	memset((void*)pages,0,sizeof(void*)*(len+1));
	//down_write(&mm_str->mmap_sem);
	ret=get_user_pages(t_str,mm_str,adr,len,0,0,pages,NULL);
	if(ret<=k)
	{
		up_write(&mm_str->mmap_sem);
		goto lerr_01;
	}*/
	while(1)
	{
		memset((void*)pages,0,sizeof(void*)*(len+1));
		down_write(&mm_str->mmap_sem);
		ret=get_user_pages(t_str,mm_str,adr,len,0,0,pages,NULL);
		if(ret<=k)
		{
			up_write(&mm_str->mmap_sem);
			goto lerr_01;
		}
		c=kmap(pages[k]);
		for(i=0;i<8;i++)
		{
			lk=(struct KVAR_LOCK *)&(k_am->ladr[i]);
			l=lk->offset.off;
			if(lk->maxd==0 && lk->mind==0)
				continue;
			if(lk->mind==0)
			{
				if(lk->maxd<0x100)
				{
					if(lk->maxd<c[l])
						c[l]=lk->maxd;
					continue;
				}
				if(lk->maxd<0x10000)
				{
					if(l>4094)
						continue;
					m=c[l]+c[l+1]*0x100;
					if(lk->maxd<m)
					{c[l]=lk->mxc[0];c[l+1]=lk->mxc[1];}
					continue;
				}
				if(lk->maxd<0x1000000)
				{
					if(l>4093)
						continue;
					m=c[l]+c[l+1]*0x100+c[l+2]*0x10000;
					if(lk->maxd<m)
					{
						c[l]=lk->mxc[0];c[l+1]=lk->mxc[1];
						c[l+2]=lk->mxc[2];
					}
					continue;
				}
				if(l>4092)
					continue;
				m=c[l]+c[l+1]*0x100+c[l+2]*0x10000+c[l+3]*0x1000000;
				if(lk->maxd<m)
				{
					c[l]=lk->mxc[0];c[l+1]=lk->mxc[1];
					c[l+2]=lk->mxc[2];c[l+3]=lk->mxc[3];
				}
				continue;
			}
			if(lk->maxd==0)
			{
				if(lk->mind<0x100)
				{
					if(lk->mind>c[l])
						c[l]=lk->mind;
					continue;
				}
				if(lk->mind<0x10000)
				{
					if(l>4094)
						continue;
					m=c[l]+c[l+1]*0x100;
					if(lk->mind>m)
					{c[l]=lk->mic[0];c[l+1]=lk->mic[1];}
					continue;
				}
				if(lk->mind<0x1000000)
				{
					if(l>4093)
						continue;
					m=c[l]+c[l+1]*0x100+c[l+2]*0x10000;
					if(lk->mind>m)
					{
						c[l]=lk->mic[0];c[l+1]=lk->mic[1];
						c[l+2]=lk->mic[2];
					}
					continue;
				}
				if(l>4092)
					continue;
				m=c[l]+c[l+1]*0x100+c[l+2]*0x10000+c[l+3]*0x1000000;
				if(lk->mind>m)
				{
					c[l]=lk->mic[0];c[l+1]=lk->mic[1];
					c[l+2]=lk->mic[2];c[l+3]=lk->mic[3];
				}
				continue;
			}
			if(lk->mind<0x100)
			{
				if(lk->mind>c[l] || lk->maxd<c[l])
					c[l]=lk->mind;
				continue;
			}
			if(lk->mind<0x10000)
			{
				if(l>4094)
					continue;
				m=c[l]+c[l+1]*0x100;
				if(lk->mind>m || lk->maxd<m)
				{c[l]=lk->mic[0];c[l+1]=lk->mic[1];}
				continue;
			}
			if(lk->mind<0x1000000)
			{
				if(l>4093)
					continue;
				m=c[l]+c[l+1]*0x100+c[l+2]*0x10000;
				if(lk->mind>m || lk->maxd<m)
				{
					c[l]=lk->mic[0];c[l+1]=lk->mic[1];
					c[l+2]=lk->mic[2];
				}
				continue;
			}
			if(l>4092)
				continue;
			m=c[l]+c[l+1]*0x100+c[l+2]*0x10000+c[l+3]*0x1000000;
			if(lk->mind>m || lk->maxd<m)
			{
				c[l]=lk->mic[0];c[l+1]=lk->mic[1];
				c[l+2]=lk->mic[2];c[l+3]=lk->mic[3];
			}
		}
		kunmap(pages[k]);
		page_cache_release(pages[k]);
		up_write(&mm_str->mmap_sem);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(6*HZ);		
		if(tp[2]==1)//结束
		{
			//page_cache_release(pages[k]);
			break;
		}	
	}
	//up_write(&mm_str->mmap_sem);
lerr_01:
	if(v!=NULL)
		vfree(v);
	kv1.pin=1;
	kv1.thread_lock=0;
	return 0;
}//}}}
//{{{ void s_sync(int t)
void s_sync(int t)
{
	switch(t)
	{
	case 0://内核忙，返回用户的读取字节数为0
		kv1.pin=0;
		return;
	case 1://首次查询，完成一页的地址
		k_am->t_seg=kv1.seg[0];
		k_am->d_seg=kv1.seg[1];
		k_am->b_seg=kv1.seg[2];
		k_am->s_seg=kv1.seg[3];
		k_am->t_len=kv1.t_len;
		k_am->d_len=kv1.d_len;
		k_am->end0=0;
		k_am->sync=1;
		kv1.pin=1;
		s_wait_mm(0);
		kv1.pin=0;
		return;
	case 2://首次查询，全部完成。
		k_am->t_seg=kv1.seg[0];
		k_am->d_seg=kv1.seg[1];
		k_am->b_seg=kv1.seg[2];
		k_am->s_seg=kv1.seg[3];
		k_am->t_len=kv1.t_len;
		k_am->d_len=kv1.d_len;
		k_am->end0=1;
		k_am->sync=1;
		kv1.pin=1;
		s_wait_mm(0);
		return;
	case 10://再次查询，传出结果。
		k_tp->end0=0;
		k_tp->end1=0;
		k_tp->t_seg=kv1.seg[0];
		k_tp->d_seg=kv1.seg[1];
		k_tp->b_seg=kv1.seg[2];
		k_tp->s_seg=kv1.seg[3];
		k_tp->t_len=kv1.t_len;
		k_tp->d_len=kv1.d_len;
		k_tp->sync=1;
		kv1.pin=2;
		s_wait_mm(1);
		kv1.pin=0;
		return;
	case 12://再次查询，接收用户输入的地址
		if(k_am->end0!=0)
		{
			k_tp->end0=1;//与case 10唯一的不同，表示全部完成。
			k_tp->end1=0;
			k_tp->t_seg=kv1.seg[0];
			k_tp->d_seg=kv1.seg[1];
			k_tp->b_seg=kv1.seg[2];
			k_tp->s_seg=kv1.seg[3];
			k_tp->t_len=kv1.t_len;
			k_tp->d_len=kv1.d_len;
			k_tp->sync=1;
			kv1.pin=2;
			s_wait_mm(1);
			kv1.pin=1;
			memset((void*)tp,0,K_BUFFER_SIZE);
			return;
		}
		k_am->t_seg=kv1.seg[0];
		k_am->d_seg=kv1.seg[1];
		k_am->b_seg=kv1.seg[2];
		k_am->s_seg=kv1.seg[3];
		k_am->t_len=kv1.t_len;
		k_am->d_len=kv1.d_len;
		k_am->sync=1;
		k_am->end0=0;
		k_am->end1=1;
		kv1.pin=1;
		s_wait_mm(0);
		kv1.pin=0;
		return;
	};

}//}}}
//{{{ void s_wait_mm(int w)
void s_wait_mm(int w)
{
	char *ch;
	//printk("<1>a sleep\n");
	if(w==0)//mp
		ch=mp;
	else
	{
		if(w==1)
			ch=tp;
		else
		{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ);
			return;
		}
	}
	while(1)
	{
		if(ch[0]==0)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}
}//}}}
//{{{  int mem_srch(void *argc)
int mem_srch(void *argc)
{
	struct pid *kpid;
	struct task_struct *t_str;
	struct mm_struct *mm_str;
	struct vm_area_struct *vadr;
	struct page **pages;
	union OFFSET oft;
	char *v,*c,*p;
	int i,j,ret,len;
	unsigned long adr;
	v=NULL;p=(char*)&(tp[d_begin]);
	daemonize("ty_thd4");
	kpid=find_get_pid(kv1.pid);
	if(kpid==NULL)
	{
		printk("<1>find_get_pid error\n");
		goto merr_01;
	}
	t_str=pid_task(kpid,PIDTYPE_PID);
	if(t_str==NULL)
	{
		printk("<1>pid_task error\n");
		goto merr_01;
	}
	mm_str=get_task_mm(t_str);
	if(mm_str==NULL)
	{
		printk("<1>get_task_mm error\n");
		goto merr_01;
	}
	i=mm_str->end_code-mm_str->start_code;
	j=mm_str->end_data-mm_str->start_data;
	if((i!=kv1.t_len) || (j!=kv1.d_len))
	{
		printk("<1>target process unequal\n");
		goto merr_01;
	}
	oft.ad=kv1.snum;
	printk("<1>seg=%d page=%d\n",oft.seg,oft.page);
	vadr=mm_str->mmap;
	for(i=0;i<8;i++)
	{
		if(i==oft.seg)
			break;
		if(vadr==NULL)
		{
			printk("<1>vadr is NULL\n");
			goto merr_01;
		}
		vadr=vadr->vm_next;
	}
	adr=vadr->vm_start;
	len=vma_pages(vadr);
	if(len<oft.page)
		goto merr_01;
	v=(char*)vmalloc(sizeof(void*)*(len+1));
	pages=(struct page **)v;
	if(pages==NULL)
	{
		printk("<1>vmalloc error\n");
		goto merr_01;
	}
	while(1)
	{
		memset((void*)pages,0,sizeof(void*)*(len+1));
		down_write(&mm_str->mmap_sem);
		ret=get_user_pages(t_str,mm_str,adr,len,0,0,pages,NULL);
		if(ret<oft.page)
		{
			up_write(&mm_str->mmap_sem);
			goto merr_01;
		}
		c=kmap(pages[oft.page]);
		memcpy(p,c,4096);
		kunmap(pages[oft.page]);
		page_cache_release(pages[oft.page]);
		up_write(&mm_str->mmap_sem);
		if(tp[2]==1)//end
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2*HZ);
	}
merr_01:
	if(v!=NULL)
		vfree(v);
	kv1.pin=1;
	kv1.thread_lock=0;
	return 0;
}//}}}





module_init(minedev_init);
module_exit(minedev_exit);




